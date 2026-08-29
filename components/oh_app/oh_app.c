#include "oh_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "oh_board.h"
#include "oh_config.h"
#include "oh_drivers.h"
#include "oh_net.h"
#include "oh_node.h"

static const char *TAG = "oh_app";

static char s_sn[9];
static char s_hostname[24];
static esp_timer_handle_t s_reboot;

const char *oh_app_sn(void)
{
    return s_sn;
}

const char *oh_app_fw(void)
{
    return esp_app_get_description()->version;
}

const char *oh_app_hostname(void)
{
    return s_hostname;
}

void oh_app_node_name(char *out, size_t cap)
{
    oh_node_lock();
    oh_config_node_name(oh_node_module(), s_sn, out, cap);
    oh_node_unlock();
}

oh_vmod_pins_t oh_app_preset_pins(void)
{
    const oh_board_pins_t *b = oh_board_pins();
    oh_vmod_pins_t p;
    memcpy(p.inputs, b->preset_inputs, sizeof(p.inputs));
    memcpy(p.outputs, b->preset_outputs, sizeof(p.outputs));
    p.strip = b->mod_fast0;
    p.mod_int = b->mod_int;
    return p;
}

/* ── Module selection ─────────────────────────────────────────────────── */

static void fill_module_sn(oh_module_t *m)
{
    if (!m->module.sn[0]) {
        snprintf(m->module.sn, sizeof(m->module.sn), "mod-%s", s_sn);
    }
}

static esp_err_t bind_and_announce(const oh_module_t *m)
{
    esp_err_t err = oh_node_bind(m);
    if (m->source == OH_MOD_SRC_NONE) {
        oh_node_error_set("module-absent");
    } else {
        oh_node_error_clear("module-absent");
    }
    oh_app_announce();
    return err;
}

/* An oh_module_t is a few kilobytes: it lives on the heap, never on a task
 * stack, and certainly not twice. */
esp_err_t oh_app_select_module(void)
{
    oh_module_t *m = calloc(1, sizeof(*m));
    if (!m) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t rc;
    /* 1. The ID EEPROM (base board): not yet — phase 5. */
    /* 2. A stored virtual module. */
    char *json = NULL;
    if (oh_config_vmod_get(&json) == ESP_OK && json) {
        char err[OH_ERR_MAX];
        bool ok = oh_vmod_parse(json, m, err);
        free(json);
        if (ok) {
            fill_module_sn(m);
            ESP_LOGI(TAG, "virtual module 0x%04x \"%s\"", m->module.type, m->module.name);
            rc = bind_and_announce(m);
            free(m);
            return rc;
        }
        ESP_LOGE(TAG, "stored virtual module is unusable (%s); running headless", err);
    }
    /* 3. Nothing: headless, but on the network and findable. */
    oh_module_init_headless(m);
    fill_module_sn(m);
    rc = bind_and_announce(m);
    free(m);
    return rc;
}

esp_err_t oh_app_set_vmod_json(const char *json, char *err)
{
    oh_module_t *m = calloc(1, sizeof(*m));
    if (!m) {
        snprintf(err, OH_ERR_MAX, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    if (!oh_vmod_parse(json, m, err)) {
        free(m);
        return ESP_ERR_INVALID_ARG;
    }
    /* Store the normalised form, so what is read back is what runs. */
    cJSON *o = oh_vmod_to_json(m);
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!text) {
        free(m);
        snprintf(err, OH_ERR_MAX, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t rc = oh_config_vmod_set(text);
    free(text);
    if (rc != ESP_OK) {
        free(m);
        snprintf(err, OH_ERR_MAX, "could not store: %s", esp_err_to_name(rc));
        return rc;
    }
    fill_module_sn(m);
    rc = bind_and_announce(m);
    free(m);
    return rc;
}

esp_err_t oh_app_set_preset(const char *key, char *err)
{
    oh_module_t *m = calloc(1, sizeof(*m));
    if (!m) {
        return ESP_ERR_NO_MEM;
    }
    oh_vmod_pins_t pins = oh_app_preset_pins();
    if (!oh_vmod_preset(key, &pins, m)) {
        free(m);
        snprintf(err, OH_ERR_MAX, "no preset \"%s\"", key);
        return ESP_ERR_NOT_FOUND;
    }
    cJSON *o = oh_vmod_to_json(m);
    free(m);
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t rc = oh_app_set_vmod_json(text, err);
    free(text);
    return rc;
}

esp_err_t oh_app_clear_vmod(void)
{
    esp_err_t err = oh_config_vmod_clear();
    if (err != ESP_OK) {
        return err;
    }
    return oh_app_select_module();
}

const char *oh_app_module_source(void)
{
    oh_node_lock();
    oh_module_source_t src = oh_node_module()->source;
    oh_node_unlock();
    return src == OH_MOD_SRC_EEPROM ? "eeprom" : src == OH_MOD_SRC_VIRTUAL ? "virtual" : "none";
}

void oh_app_announce(void)
{
    char name[OH_NAME_MAX];
    oh_app_node_name(name, sizeof(name));
    oh_node_lock();
    esp_err_t err = oh_mdns_update(oh_node_module(), name, oh_app_fw(), s_sn);
    oh_node_unlock();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mdns txt: %s", esp_err_to_name(err));
    }
}

/* ── Reacting to config ───────────────────────────────────────────────── */

static void on_config_changed(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    uint32_t mask = *(uint32_t *)data;
    if (mask & OH_CFG_NAME) {
        oh_app_announce();
    }
    if (mask & OH_CFG_WIFI) {
        oh_net_apply_wifi();
    }
    if (mask & OH_CFG_NETWORK) {
        oh_net_apply_ip();
    }
}

/* ── Lifecycle ────────────────────────────────────────────────────────── */

static void do_reboot(void *arg)
{
    (void)arg;
    esp_restart();
}

void oh_app_reboot(uint32_t delay_ms)
{
    ESP_LOGW(TAG, "rebooting in %lu ms", (unsigned long)delay_ms);
    if (!s_reboot) {
        esp_timer_create_args_t args = {.callback = do_reboot, .name = "reboot"};
        esp_timer_create(&args, &s_reboot);
    }
    esp_timer_stop(s_reboot);
    esp_timer_start_once(s_reboot, (uint64_t)delay_ms * 1000);
}

void oh_app_factory_reset(void)
{
    ESP_LOGW(TAG, "factory reset");
    oh_config_erase_all();
    oh_app_reboot(500);
}

esp_err_t oh_app_init(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_sn, sizeof(s_sn), "%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_hostname, sizeof(s_hostname), "openhaunt-%s", s_sn);
    ESP_LOGI(TAG, "OpenHaunt node %s, fw %s, board %s", s_sn, oh_app_fw(), oh_board_name());

    ESP_ERROR_CHECK(oh_config_init());
    ESP_ERROR_CHECK(oh_node_init());
    oh_drivers_register_all();
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, OH_EVENT_CONFIG_CHANGED, on_config_changed, NULL));
    return oh_app_select_module();
}

esp_err_t oh_app_start_network(void)
{
    esp_err_t err = oh_net_init(s_hostname);
    if (err != ESP_OK) {
        return err;
    }
    err = oh_mdns_start(s_sn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns: %s", esp_err_to_name(err));
        return err;
    }
    oh_app_announce();
    return ESP_OK;
}
