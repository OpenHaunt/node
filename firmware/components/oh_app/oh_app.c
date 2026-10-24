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
#include "oh_descriptor.h"
#include "oh_config.h"
#include "oh_drivers.h"
#include "oh_module_eeprom.h"
#include "oh_ota.h"
#include "oh_sacn.h"
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
    p.uart_tx = b->mod_uart_tx;
    p.uart_rx = b->mod_uart_rx;
    p.uart_de = b->mod_uart_de;
    return p;
}

oh_vmod_board_io_t oh_app_board_io(void)
{
    const oh_board_pins_t *b = oh_board_pins();
    oh_vmod_board_io_t io;
    memcpy(io.relays, b->relays, sizeof(io.relays));
    memcpy(io.inputs, b->sensors, sizeof(io.inputs));
    io.input_contact_low = b->sensor_contact_low;
    memcpy(io.strips, b->strips, sizeof(io.strips));
    io.strip_count = b->strip_pixels;
    for (int n = 0; n < 2; n++) {
        /* Nothing stored leaves the count at zero: the builder's defaults. */
        oh_config_pixels_get(n, &io.strip_cfg[n]);
    }
    io.rev = oh_board_rev();
    return io;
}

/* ── Strips (doc 14) ──────────────────────────────────────────────────── */

/* Whether the strips are the board's to describe: a stored virtual module wins. */
static bool board_owns_strips(void)
{
    if (!oh_board_has_builtin_io()) {
        return false;
    }
    oh_node_lock();
    const bool board = oh_node_module()->source == OH_MOD_SRC_BOARD;
    oh_node_unlock();
    return board;
}

cJSON *oh_app_pixels_json(bool with_status)
{
    if (!board_owns_strips()) {
        return NULL;
    }
    const oh_vmod_board_io_t io = oh_app_board_io();
    oh_pix_cfg_t cfgs[2] = {oh_vmod_board_strip_cfg(&io, 0), oh_vmod_board_strip_cfg(&io, 1)};
    const uint8_t heads[2] = {1, 2};
    cJSON *o = oh_pix_info_json(cfgs, heads, 2);
    if (with_status) {
        cJSON *strip;
        int n = 0;
        cJSON_ArrayForEach(strip, cJSON_GetObjectItem(o, "strips")) {
            oh_ws2812_status_t st = {0};
            cJSON *status = cJSON_AddObjectToObject(strip, "status");
            const bool bound = io.strips[n] >= 0 && oh_ws2812_status(io.strips[n], &st);
            cJSON_AddBoolToObject(status, "bound", bound);
            cJSON_AddNumberToObject(status, "frames", st.frames);
            cJSON_AddNumberToObject(status, "sources", st.sources);
            cJSON_AddBoolToObject(status, "holding", st.holding);
            cJSON_AddNumberToObject(status, "fps", (int)(st.fps * 10.0f + 0.5f) / 10.0);
            n++;
        }
    }
    return o;
}

esp_err_t oh_app_set_pixels(int output, const cJSON *body, char *err, cJSON **strip_out)
{
    *strip_out = NULL;
    if (output < 0 || output >= 2 || !oh_board_has_builtin_io()) {
        snprintf(err, OH_ERR_MAX, "no strip output %d", output);
        return ESP_ERR_NOT_FOUND;
    }
    if (!board_owns_strips()) {
        snprintf(err, OH_ERR_MAX, "a stored virtual module describes this node; its strips are its own");
        return ESP_ERR_INVALID_STATE;
    }
    const oh_vmod_board_io_t io = oh_app_board_io();
    oh_pix_cfg_t cfg = oh_vmod_board_strip_cfg(&io, output);
    if (!oh_pix_cfg_apply_json(&cfg, body, err)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t rc = oh_config_pixels_set(output, &cfg);
    if (rc != ESP_OK) {
        snprintf(err, OH_ERR_MAX, "could not store: %s", esp_err_to_name(rc));
        return rc;
    }
    ESP_LOGI(TAG, "strip %d: %s, %u pixels, %s", output, oh_pix_mode_str(cfg.mode), cfg.count, cfg.order);
    /* The description follows from the settings, so the node describes and binds
     * itself again, the way applying a virtual module does. */
    rc = oh_app_select_module();
    *strip_out = oh_pix_cfg_to_json(&cfg, (uint8_t)output, (uint8_t)(output + 1));
    return rc;
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
    /* 1. The ID EEPROM on the module bus. The module stays in reset until it
     *    is known and within budget. */
    oh_desc_result_t result;
    uint32_t power_mw;
    rc = oh_eeprom_describe(m, &result, &power_mw);
    if (rc == ESP_OK) {
        fill_module_sn(m);
        oh_node_error_clear("power-budget");
        oh_node_error_clear("descriptor");
        rc = bind_and_announce(m);
        oh_module_reset_release();
        free(m);
        return rc;
    }
    if (rc == ESP_ERR_INVALID_SIZE) {
        /* Described, over budget: report it, drive nothing, keep it in reset. */
        m->n_bindings = 0;
        fill_module_sn(m);
        rc = bind_and_announce(m);
        oh_node_error_set("power-budget");
        free(m);
        return rc;
    }
    if (rc == ESP_ERR_INVALID_CRC || rc == ESP_ERR_INVALID_VERSION) {
        oh_node_error_set("descriptor");
    } else {
        oh_node_error_clear("descriptor");
    }
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
    /* 3. The board's own I/O, where it has some. */
    if (oh_board_has_builtin_io()) {
        oh_vmod_board_io_t io = oh_app_board_io();
        if (oh_vmod_board_v0(&io, m)) {
            fill_module_sn(m);
            ESP_LOGI(TAG, "built-in I/O \"%s\" rev %s", m->module.name, m->module.rev);
            rc = bind_and_announce(m);
            free(m);
            return rc;
        }
        ESP_LOGE(TAG, "built-in I/O does not describe; running headless");
    }
    /* 4. Nothing: headless, but on the network and findable. */
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
    /* Store the normalized form, so what is read back is what runs. */
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
    switch (src) {
    case OH_MOD_SRC_EEPROM:
        return "eeprom";
    case OH_MOD_SRC_VIRTUAL:
        return "virtual";
    case OH_MOD_SRC_BOARD:
        return "board";
    default:
        return "none";
    }
}

void oh_app_announce(void)
{
    char name[OH_NAME_MAX];
    oh_app_node_name(name, sizeof(name));
    /* `desc` (doc 14): a checksum over what the node says it is — module, ports,
     * heads, strips — so a console knows to fetch /api/v1/info again when a strip
     * changes mode, without polling. The identity is left out: it is in the TXT
     * record already and is not the description. */
    char desc[8] = "";
    oh_node_identity_t id = {.sn = "", .fw = "", .name = ""};
    oh_node_lock();
    cJSON *info = oh_json_info(oh_node_module(), &id);
    oh_node_unlock();
    cJSON *pixels = oh_app_pixels_json(false);
    if (pixels) {
        cJSON_AddItemToObject(info, "pixels", pixels);
    }
    char *text = cJSON_PrintUnformatted(info);
    cJSON_Delete(info);
    if (text) {
        snprintf(desc, sizeof(desc), "%04x", oh_crc16((const uint8_t *)text, strlen(text)));
        free(text);
    }
    oh_node_lock();
    esp_err_t err = oh_mdns_update(oh_node_module(), name, oh_app_fw(), s_sn, desc);
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
    oh_module_reset_hold();
    oh_board_init();
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_sn, sizeof(s_sn), "%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_hostname, sizeof(s_hostname), "openhaunt-%s", s_sn);
    ESP_LOGI(TAG, "OpenHaunt node %s, fw %s, board %s", s_sn, oh_app_fw(), oh_board_name());

    ESP_ERROR_CHECK(oh_config_init());
    ESP_ERROR_CHECK(oh_node_init());
    oh_drivers_register_all();
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, OH_EVENT_CONFIG_CHANGED, on_config_changed, NULL));
    ESP_ERROR_CHECK(oh_ota_start());
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
    return oh_sacn_start();
}
