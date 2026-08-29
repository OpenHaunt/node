/*
 * ssd1306: a 128x64 (or 128x32) OLED over I2C with one string port — the
 * display module (0x0005). Lines are separated by '\n'.
 *
 *   { "driver": "ssd1306", "port": 0, "addr": 60, "height": 64 }
 */
#include <string.h>

#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"
#include "oh_driver.h"
#include "oh_font.h"
#include "oh_i2c.h"

static const char *TAG = "ssd1306";

#define W 128

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    int height;
    uint8_t *buf; /* W * height / 8, page-organised as the controller wants it */
    char text[OH_TEXT_MAX];
} inst_t;

static void put_pixel(inst_t *i, int x, int y, bool on)
{
    if (x < 0 || x >= W || y < 0 || y >= i->height) {
        return;
    }
    uint8_t *b = &i->buf[(y / 8) * W + x];
    if (on) {
        *b |= (uint8_t)(1 << (y % 8));
    } else {
        *b &= (uint8_t)~(1 << (y % 8));
    }
}

static void draw_text(inst_t *i, int x, int y, const char *text)
{
    for (const char *c = text; *c && x < W; c++, x += OH_FONT_W) {
        unsigned ch = (unsigned char)*c;
        const uint8_t *glyph = OH_FONT[(ch >= 0x20 && ch < 0x7F) ? ch - 0x20 : '?' - 0x20];
        for (int gy = 0; gy < OH_FONT_H; gy++) {
            for (int gx = 0; gx < OH_FONT_W; gx++) {
                put_pixel(i, x + gx, y + gy, glyph[gy] & (0x80 >> gx));
            }
        }
    }
}

static esp_err_t show(inst_t *i)
{
    memset(i->buf, 0, W * i->height / 8);
    char copy[OH_TEXT_MAX];
    strncpy(copy, i->text, sizeof(copy));
    copy[sizeof(copy) - 1] = '\0';
    int y = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line && y + OH_FONT_H <= i->height;
         line = strtok_r(NULL, "\n", &save)) {
        draw_text(i, 0, y, line);
        y += OH_FONT_H;
    }
    return esp_lcd_panel_draw_bitmap(i->panel, 0, 0, W, i->height, i->buf);
}

static esp_err_t init(const oh_driver_bind_t *bind, void **out)
{
    inst_t *i = calloc(1, sizeof(*i));
    if (!i) {
        return ESP_ERR_NO_MEM;
    }
    int addr = oh_driver_param_int(bind->params, "addr", 0x3C);
    i->height = oh_driver_param_int(bind->params, "height", 64) == 32 ? 32 : 64;
    i->buf = calloc(W * i->height / 8, 1);
    if (!i->buf) {
        free(i);
        return ESP_ERR_NO_MEM;
    }
    i2c_master_bus_handle_t bus;
    esp_err_t err = oh_i2c_bus(&bus);
    if (err != ESP_OK) {
        goto fail;
    }
    esp_lcd_panel_io_i2c_config_t io = {
        .dev_addr = addr,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    err = esp_lcd_new_panel_io_i2c(bus, &io, &i->io);
    if (err != ESP_OK) {
        goto fail;
    }
    esp_lcd_panel_ssd1306_config_t vendor = {.height = i->height};
    esp_lcd_panel_dev_config_t panel = {.reset_gpio_num = -1, .bits_per_pixel = 1, .vendor_config = &vendor};
    err = esp_lcd_new_panel_ssd1306(i->io, &panel, &i->panel);
    if (err != ESP_OK) {
        goto fail;
    }
    if ((err = esp_lcd_panel_reset(i->panel)) != ESP_OK || (err = esp_lcd_panel_init(i->panel)) != ESP_OK ||
        (err = esp_lcd_panel_disp_on_off(i->panel, true)) != ESP_OK) {
        ESP_LOGE(TAG, "no display at 0x%02x (%s)", addr, esp_err_to_name(err));
        goto fail;
    }
    if (bind->n_ports && bind->ports[0] && bind->ports[0]->has_default) {
        i->text[0] = '\0';
    }
    show(i);
    ESP_LOGI(TAG, "128x%d at 0x%02x", i->height, addr);
    *out = i;
    return ESP_OK;
fail:
    if (i->panel) {
        esp_lcd_panel_del(i->panel);
    }
    if (i->io) {
        esp_lcd_panel_io_del(i->io);
    }
    free(i->buf);
    free(i);
    return err;
}

static esp_err_t write(void *arg, uint8_t local, const oh_value_t *in)
{
    inst_t *i = arg;
    if (local != 0 || in->type != OH_DT_STRING) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(i->text, in->text, sizeof(i->text) - 1);
    return show(i);
}

static esp_err_t read(void *arg, uint8_t local, oh_value_t *out)
{
    inst_t *i = arg;
    if (local != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    out->type = OH_DT_STRING;
    strncpy(out->text, i->text, sizeof(out->text));
    return ESP_OK;
}

static void deinit(void *arg)
{
    inst_t *i = arg;
    esp_lcd_panel_disp_on_off(i->panel, false);
    esp_lcd_panel_del(i->panel);
    esp_lcd_panel_io_del(i->io);
    free(i->buf);
    free(i);
}

const oh_driver_t oh_driver_ssd1306 = {.id = "ssd1306", .init = init, .read = read, .write = write, .deinit = deinit};
