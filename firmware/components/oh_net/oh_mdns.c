#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mdns.h"
#include "oh_net.h"

static const char *TAG = "oh_mdns";
static bool s_started;
static char s_primary[32];

/*
 * The friendly name as a second hostname: coffin-room-dmx.local beside
 * openhaunt-<sn>.local, which stays so a bookmark or script survives a rename.
 * mDNS answers a delegated host with the addresses it was handed rather than the
 * interface's own, so they are handed over again whenever the network changes.
 * Not probed: two nodes given the same name both answer, and a resolver takes one.
 */
static struct {
    SemaphoreHandle_t lock;
    char want[33];       /* "" for none */
    char registered[33]; /* what mDNS holds now */
} s_alias;

static void alias_apply(void)
{
    if (!s_started) {
        return; /* oh_mdns_start applies it */
    }
    xSemaphoreTake(s_alias.lock, portMAX_DELAY);
    oh_net_address_t addrs[2];
    const size_t n = oh_net_addresses(addrs, 2);
    mdns_ip_addr_t list[2] = {0};
    for (size_t i = 0; i < n; i++) {
        list[i].addr.type = ESP_IPADDR_TYPE_V4;
        esp_netif_str_to_ip4(addrs[i].ip, &list[i].addr.u_addr.ip4);
        list[i].next = i + 1 < n ? &list[i + 1] : NULL;
    }
    const bool want = s_alias.want[0] && n > 0 && strcmp(s_alias.want, s_primary) != 0;
    if (s_alias.registered[0] && (!want || strcmp(s_alias.registered, s_alias.want) != 0)) {
        mdns_delegate_hostname_remove(s_alias.registered);
        ESP_LOGI(TAG, "%s.local withdrawn", s_alias.registered);
        s_alias.registered[0] = '\0';
    }
    if (want) {
        esp_err_t err;
        if (s_alias.registered[0]) {
            err = mdns_delegate_hostname_set_address(s_alias.want, list);
        } else {
            err = mdns_delegate_hostname_add(s_alias.want, list);
            if (err == ESP_OK) {
                strcpy(s_alias.registered, s_alias.want);
                ESP_LOGI(TAG, "also answering as %s.local", s_alias.want);
            }
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "alias %s.local: %s", s_alias.want, esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_alias.lock);
}

void oh_mdns_set_alias(const char *alias)
{
    if (s_alias.lock) {
        xSemaphoreTake(s_alias.lock, portMAX_DELAY);
    }
    snprintf(s_alias.want, sizeof(s_alias.want), "%s", alias ? alias : "");
    if (s_alias.lock) {
        xSemaphoreGive(s_alias.lock);
    }
    alias_apply();
}

void oh_mdns_refresh_alias(void)
{
    alias_apply();
}

esp_err_t oh_mdns_start(const char *sn)
{
    char name[32];
    snprintf(name, sizeof(name), "openhaunt-%s", sn);
    snprintf(s_primary, sizeof(s_primary), "%s", name);
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        return err;
    }
    ESP_ERROR_CHECK(mdns_hostname_set(name));
    ESP_ERROR_CHECK(mdns_instance_name_set(name));
    err = mdns_service_add(name, "_openhaunt", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    s_alias.lock = xSemaphoreCreateMutex();
    s_started = true;
    ESP_LOGI(TAG, "advertising %s._openhaunt._tcp", name);
    alias_apply();
    return ESP_OK;
}

esp_err_t oh_mdns_update(const oh_module_t *module, const char *name, const char *fw, const char *sn,
                         const char *desc)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    char type[8];
    snprintf(type, sizeof(type), "0x%04x", module->module.type);
    mdns_txt_item_t txt[] = {
        {"v", "1"},
        {"fw", (char *)fw},
        {"sn", (char *)sn},
        {"mod", type},
        {"modname", (char *)module->module.name},
        {"modsn", (char *)module->module.sn},
        {"modrev", (char *)module->module.rev},
        {"caps", (char *)module->module.caps},
        {"name", (char *)name},
        {"desc", (char *)(desc ? desc : "")},
        {"mains", "1"},
    };
    size_t n = sizeof(txt) / sizeof(txt[0]);
    /* `mains` is present only when the module switches mains: absent is the
     * signal, not "0". */
    if (!oh_module_switches_mains(module)) {
        n--;
    }

    return mdns_service_txt_set("_openhaunt", "_tcp", txt, n);
}
