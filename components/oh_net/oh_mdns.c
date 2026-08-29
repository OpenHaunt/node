#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "mdns.h"
#include "oh_net.h"

static const char *TAG = "oh_mdns";
static bool s_started;

esp_err_t oh_mdns_start(const char *sn)
{
    char name[32];
    snprintf(name, sizeof(name), "openhaunt-%s", sn);
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
    s_started = true;
    ESP_LOGI(TAG, "advertising %s._openhaunt._tcp", name);
    return ESP_OK;
}

esp_err_t oh_mdns_update(const oh_module_t *module, const char *name, const char *fw, const char *sn)
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
