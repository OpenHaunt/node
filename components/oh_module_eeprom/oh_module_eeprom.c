#include "oh_module_eeprom.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oh_board.h"
#include "oh_config.h"
#include "oh_i2c.h"
#include "oh_vmod.h"
#include "sdkconfig.h"

static const char *TAG = "oh_eeprom";
#define EEPROM_ADDR 0x50
#define PAGE 8

void oh_module_reset_hold(void)
{
    int pin = oh_board_pins()->mod_nreset;
    if (pin < 0) {
        return;
    }
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&cfg);
    gpio_set_level(pin, 0);
}

void oh_module_reset_release(void)
{
    int pin = oh_board_pins()->mod_nreset;
    if (pin >= 0) {
        gpio_set_level(pin, 1);
        ESP_LOGI(TAG, "MOD_nRESET released");
    }
}

bool oh_eeprom_present(void)
{
    return oh_i2c_probe(EEPROM_ADDR);
}

esp_err_t oh_eeprom_read(uint8_t raw[OH_EEPROM_SIZE])
{
    i2c_master_dev_handle_t dev;
    esp_err_t err = oh_i2c_device(EEPROM_ADDR, 100000, &dev);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t addr = 0;
    err = i2c_master_transmit_receive(dev, &addr, 1, raw, OH_EEPROM_SIZE, 100);
    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t oh_eeprom_write_descriptor(const oh_eeprom_descriptor_t *d)
{
    uint8_t raw[OH_DESC_SIZE];
    oh_descriptor_build(d, raw);
    i2c_master_dev_handle_t dev;
    esp_err_t err = oh_i2c_device(EEPROM_ADDR, 100000, &dev);
    if (err != ESP_OK) {
        return err;
    }
    for (int off = 0; off < OH_DESC_SIZE && err == ESP_OK; off += PAGE) {
        uint8_t buf[1 + PAGE];
        int n = OH_DESC_SIZE - off < PAGE ? OH_DESC_SIZE - off : PAGE;
        buf[0] = (uint8_t)off;
        memcpy(buf + 1, raw + off, n);
        err = i2c_master_transmit(dev, buf, 1 + n, 100);
        vTaskDelay(pdMS_TO_TICKS(6)); /* write cycle */
    }
    i2c_master_bus_rm_device(dev);
    return err;
}

/* ── The catalogue, as this firmware can drive it ─────────────────────── */

static const char *preset_for(uint16_t type)
{
    switch (type) {
    case 0x0001:
        return "dmx";
    case 0x0002:
        return "input";
    case 0x0003:
        return "led";
    case 0x0004:
        return "relay";
    case 0x0005:
        return "oled";
    case 0x0006:
        return "contact";
    case 0x0007:
        return "env";
    default:
        return NULL;
    }
}

bool oh_module_table_build(uint16_t type, oh_module_t *out)
{
    const char *key = preset_for(type);
    if (!key) {
        return false;
    }
#ifndef CONFIG_OH_DRIVER_DMX
    if (type == 0x0001) {
        /* No gateway driver in this build: say what the module is, but do
         * not offer a universe nobody would forward. */
        oh_vmod_preset(key, NULL, out);
        out->has_dmx = false;
        out->n_bindings = 0;
        return true;
    }
#endif
    /* The ports are the preset's; the bindings are the real module's. */
    oh_vmod_preset(key, NULL, out);
    out->n_bindings = 0;
    const oh_board_pins_t *pins = oh_board_pins();
    char params[OH_BINDING_PARAMS_MAX];
    switch (type) {
    case 0x0001: {
        snprintf(params, sizeof(params), "{\"driver\":\"dmx_uart\",\"ports\":[],\"uart\":1,\"tx\":%d,\"rx\":%d,\"de\":%d}",
                 pins->mod_uart_tx, pins->mod_uart_rx, pins->mod_uart_de);
        oh_module_add_binding(out, "dmx_uart", NULL, 0, params);
        break;
    }
    case 0x0002: {
        uint8_t ports[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        snprintf(params, sizeof(params),
                 "{\"driver\":\"mcp23017_in\",\"ports\":[0,1,2,3,4,5,6,7],\"addr\":32,\"int_gpio\":%d,\"invert\":true}",
                 pins->mod_int);
        oh_module_add_binding(out, "mcp23017_in", ports, 8, params);
        break;
    }
    case 0x0003: {
        uint8_t ports[2] = {0, 1};
        snprintf(params, sizeof(params), "{\"driver\":\"ws2812\",\"ports\":[0,1],\"gpio\":%d,\"count\":%u}",
                 pins->mod_fast0, oh_config_ws_count());
        oh_module_add_binding(out, "ws2812", ports, 2, params);
        break;
    }
    case 0x0004: {
        /* Doc 05 leaves "I2C or GPIO" open for the relay; an expander output
         * on the module bus is assumed until the module exists. */
        uint8_t ports[1] = {0};
        snprintf(params, sizeof(params), "{\"driver\":\"mcp23017_out\",\"ports\":[0],\"addr\":32,\"pins\":[0]}");
        oh_module_add_binding(out, "mcp23017_out", ports, 1, params);
        break;
    }
    case 0x0005: {
        uint8_t ports[1] = {0};
        snprintf(params, sizeof(params), "{\"driver\":\"ssd1306\",\"ports\":[0],\"addr\":60,\"height\":64}");
        oh_module_add_binding(out, "ssd1306", ports, 1, params);
        break;
    }
    case 0x0006: {
        uint8_t ports[4] = {0, 1, 2, 3};
        snprintf(params, sizeof(params),
                 "{\"driver\":\"mcp23017_out\",\"ports\":[0,1,2,3],\"addr\":32,\"pins\":[0,1,2,3]}");
        oh_module_add_binding(out, "mcp23017_out", ports, 4, params);
        break;
    }
    case 0x0007: {
        uint8_t ports[2] = {0, 1};
        snprintf(params, sizeof(params), "{\"driver\":\"sht4x\",\"ports\":[0,1],\"addr\":68}");
        oh_module_add_binding(out, "sht4x", ports, 2, params);
        break;
    }
    default:
        break;
    }
    return true;
}

esp_err_t oh_eeprom_describe(oh_module_t *out, oh_desc_result_t *result, uint32_t *power_mw)
{
    *result = OH_DESC_BAD_MAGIC;
    *power_mw = 0;
    if (!oh_eeprom_present()) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t raw[OH_EEPROM_SIZE];
    esp_err_t err = oh_eeprom_read(raw);
    if (err != ESP_OK) {
        return err;
    }
    oh_eeprom_descriptor_t d;
    *result = oh_descriptor_parse(raw, sizeof(raw), &d);
    if (*result != OH_DESC_OK) {
        ESP_LOGE(TAG, "descriptor: %s", oh_descriptor_result_str(*result));
        return *result == OH_DESC_BAD_CRC ? ESP_ERR_INVALID_CRC : ESP_ERR_INVALID_VERSION;
    }
    if (!oh_module_table_build(d.module_type, out)) {
        ESP_LOGW(TAG, "module type 0x%04x has no driver in this build", d.module_type);
        memset(out, 0, sizeof(*out));
        out->module.type = d.module_type;
    }
    /* Identity comes from the EEPROM, not the table. */
    out->source = OH_MOD_SRC_EEPROM;
    out->module.type = d.module_type;
    out->module.flags = d.capability_flags;
    oh_descriptor_field(d.name, sizeof(d.name), out->module.name, sizeof(out->module.name));
    if (!out->module.name[0]) {
        strncpy(out->module.name, "Module", OH_NAME_MAX - 1);
    }
    snprintf(out->module.rev, sizeof(out->module.rev), "%u", d.hw_revision);
    char uid[2 * OH_DESC_UID_LEN + 1];
    if (oh_descriptor_uid_hex(raw, sizeof(raw), uid, sizeof(uid))) {
        /* The low four bytes, as the doc's eight-hex example. */
        strncpy(out->module.sn, uid + 8, sizeof(out->module.sn) - 1);
    }
    *power_mw = oh_descriptor_power_mw(&d);
    ESP_LOGI(TAG, "module 0x%04x \"%s\" rev %u, %lu mW declared, flags 0x%04x, sn %s", d.module_type,
             out->module.name, d.hw_revision, (unsigned long)*power_mw, d.capability_flags, out->module.sn);
    if (*power_mw > CONFIG_OH_POWER_BUDGET_MW) {
        ESP_LOGE(TAG, "declared %lu mW exceeds the %d mW budget; module stays in reset", (unsigned long)*power_mw,
                 CONFIG_OH_POWER_BUDGET_MW);
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}
