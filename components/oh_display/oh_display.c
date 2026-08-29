#include "oh_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "oh_app.h"
#include "oh_clock.h"
#include "oh_config.h"
#include "oh_fb.h"
#include "oh_font.h"
#include "oh_mqtt.h"
#include "oh_net.h"
#include "oh_node.h"
#include "sdkconfig.h"

#ifdef CONFIG_OH_DISPLAY_BUS_SPI
#include "driver/spi_master.h"
#else
#include "esp_lcd_panel_io_interface.h"
#endif

static const char *TAG = "oh_display";

#define DIRTY_BIT (1 << 0)
#define ROW_H OH_FONT_H

static struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    oh_fb_t fb;
    int rotation; /* 0 = landscape, connector on the left; 1..3 quarter turns */
    EventGroupHandle_t events;
    int64_t identify_until_us;
    bool inverted;
} s;

static int landscape(int rotation)
{
    return (rotation & 1) == 0;
}

/* ── Panel ────────────────────────────────────────────────────────────── */

static void apply_rotation(void)
{
    /* Native orientation is portrait (width CONFIG_OH_DISPLAY_WIDTH). */
    bool land = landscape(s.rotation);
    s.fb.w = land ? CONFIG_OH_DISPLAY_HEIGHT : CONFIG_OH_DISPLAY_WIDTH;
    s.fb.h = land ? CONFIG_OH_DISPLAY_WIDTH : CONFIG_OH_DISPLAY_HEIGHT;
    esp_lcd_panel_swap_xy(s.panel, land);
    switch (s.rotation) {
    case 0:
        esp_lcd_panel_mirror(s.panel, false, true);
        break;
    case 1:
        esp_lcd_panel_mirror(s.panel, false, false);
        break;
    case 2:
        esp_lcd_panel_mirror(s.panel, true, false);
        break;
    default:
        esp_lcd_panel_mirror(s.panel, true, true);
        break;
    }
    /* The 170 visible columns sit at an offset in the controller's 240-wide
     * RAM; after swap_xy that offset is on the panel's y. */
    if (land) {
        esp_lcd_panel_set_gap(s.panel, 0, CONFIG_OH_DISPLAY_X_GAP);
    } else {
        esp_lcd_panel_set_gap(s.panel, CONFIG_OH_DISPLAY_X_GAP, 0);
    }
}

static esp_err_t panel_init(void)
{
#ifdef CONFIG_OH_DISPLAY_BUS_SPI
    spi_bus_config_t bus = {
        .sclk_io_num = CONFIG_OH_DISPLAY_SPI_SCLK,
        .mosi_io_num = CONFIG_OH_DISPLAY_SPI_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_OH_DISPLAY_HEIGHT * ROW_H * 2 * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");
    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = CONFIG_OH_DISPLAY_DC_GPIO,
        .cs_gpio_num = CONFIG_OH_DISPLAY_SPI_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &s.io), TAG, "panel io");
#else
    int data[8] = {0};
    {
        const char *p = CONFIG_OH_DISPLAY_I80_DATA;
        for (int i = 0; i < 8 && *p; i++) {
            data[i] = (int)strtol(p, (char **)&p, 10);
            while (*p == ',' || *p == ' ') {
                p++;
            }
        }
    }
    esp_lcd_i80_bus_handle_t i80;
    esp_lcd_i80_bus_config_t bus = {
        .dc_gpio_num = CONFIG_OH_DISPLAY_DC_GPIO,
        .wr_gpio_num = CONFIG_OH_DISPLAY_I80_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]},
        .bus_width = 8,
        .max_transfer_bytes = CONFIG_OH_DISPLAY_HEIGHT * ROW_H * 2 * 2,
        .dma_burst_size = 64,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus, &i80), TAG, "i80 bus");
    gpio_config_t rd = {.pin_bit_mask = 1ULL << CONFIG_OH_DISPLAY_I80_RD, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rd);
    gpio_set_level(CONFIG_OH_DISPLAY_I80_RD, 1);
    esp_lcd_panel_io_i80_config_t io = {
        .cs_gpio_num = -1,
        .pclk_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels = {.dc_idle_level = 0, .dc_cmd_level = 0, .dc_dummy_level = 0, .dc_data_level = 1},
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(i80, &io, &s.io), TAG, "panel io");
#endif
    esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = CONFIG_OH_DISPLAY_RST_GPIO,
#ifdef CONFIG_OH_DISPLAY_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s.io, &panel, &s.panel), TAG, "st7789");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s.panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s.panel), TAG, "init");
#ifdef CONFIG_OH_DISPLAY_INVERT
    esp_lcd_panel_invert_color(s.panel, true);
    s.inverted = true;
#endif
    apply_rotation();
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s.panel, true), TAG, "on");
    if (CONFIG_OH_DISPLAY_BL_GPIO >= 0) {
        gpio_config_t bl = {.pin_bit_mask = 1ULL << CONFIG_OH_DISPLAY_BL_GPIO, .mode = GPIO_MODE_OUTPUT};
        gpio_config(&bl);
        gpio_set_level(CONFIG_OH_DISPLAY_BL_GPIO, 1);
    }
    return ESP_OK;
}

static void flush(void)
{
    /* Whole frame, in row bands the SPI transfer size allows. */
    int band = ROW_H * 2;
    for (int y = 0; y < s.fb.h; y += band) {
        int h = (y + band > s.fb.h) ? s.fb.h - y : band;
        esp_lcd_panel_draw_bitmap(s.panel, 0, y, s.fb.w, y + h, s.fb.px + y * s.fb.w);
    }
}

/* ── Content ──────────────────────────────────────────────────────────── */

static const char *value_str(const oh_port_desc_t *p, const oh_value_t *v, char *buf, size_t cap)
{
    switch (v->type) {
    case OH_DT_BOOLEAN:
        return v->state ? "ON" : "off";
    case OH_DT_NUMBER:
        snprintf(buf, cap, "%.2f%s", v->value, strcmp(p->unit, "percent") == 0 ? "%" : "");
        return buf;
    case OH_DT_COLOR:
        snprintf(buf, cap, "%02x%02x%02x", v->color.r, v->color.g, v->color.b);
        return buf;
    case OH_DT_STRING:
        snprintf(buf, cap, "\"%.12s\"", v->text);
        return buf;
    }
    return "";
}

static void render(void)
{
    const uint16_t bg = oh_rgb(0, 0, 0), fg = oh_rgb(230, 230, 230), dim = oh_rgb(120, 120, 120);
    const uint16_t hdr = oh_rgb(210, 120, 40), ok = oh_rgb(60, 200, 80), bad = oh_rgb(230, 60, 60);
    const uint16_t warn = oh_rgb(230, 170, 40);
    int cols = s.fb.w / OH_FONT_W;
    char line[64];

    oh_fb_fill(&s.fb, 0, 0, s.fb.w, s.fb.h, bg);

    /* Header bar: hostname and firmware. */
    oh_fb_fill(&s.fb, 0, 0, s.fb.w, ROW_H, hdr);
    snprintf(line, sizeof(line), " %s", oh_app_hostname());
    oh_fb_text(&s.fb, 0, 0, 1, oh_rgb(0, 0, 0), hdr, line);
    snprintf(line, sizeof(line), "fw %s ", oh_app_fw());
    oh_fb_text(&s.fb, s.fb.w - (int)strlen(line) * OH_FONT_W, 0, 1, oh_rgb(0, 0, 0), hdr, line);

    int y = ROW_H + 2;
    char ip[16], ssid[33], ap[24];
    oh_net_ip(ip, sizeof(ip));
    oh_net_ssid(ssid, sizeof(ssid));
    oh_net_ap_ssid(ap, sizeof(ap));
    uint16_t net_colour = fg;
    switch (oh_net_state()) {
    case OH_NET_UP:
        snprintf(line, sizeof(line), "%s %s  %.*s", oh_net_transport(), ip, cols - 17 - (int)strlen(ip), ssid);
        break;
    case OH_NET_SETUP_AP:
        snprintf(line, sizeof(line), "SETUP AP %s", ap);
        net_colour = warn;
        break;
    case OH_NET_CONNECTING:
        snprintf(line, sizeof(line), "connecting to %.*s", cols - 14, ssid[0] ? ssid : "wifi");
        net_colour = warn;
        break;
    default:
        snprintf(line, sizeof(line), "no network");
        net_colour = bad;
        break;
    }
    oh_fb_text(&s.fb, 0, y, 1, net_colour, bg, line);
    y += ROW_H;

    oh_config_t cfg;
    oh_config_get(&cfg);
    if (oh_net_state() == OH_NET_SETUP_AP) {
        snprintf(line, sizeof(line), "join it, open http://192.168.4.1/");
    } else if (!cfg.mqtt_broker[0]) {
        snprintf(line, sizeof(line), "not adopted: no broker yet");
    } else {
        snprintf(line, sizeof(line), "mqtt %.40s %s", cfg.mqtt_broker, oh_mqtt_connected() ? "OK" : "...");
    }
    oh_fb_text(&s.fb, 0, y, 1, cfg.mqtt_broker[0] && oh_mqtt_connected() ? ok : dim, bg, line);
    y += ROW_H;

    oh_node_lock();
    const oh_module_t *m = oh_node_module();
    const oh_port_state_t *st = oh_node_states();
    snprintf(line, sizeof(line), "0x%04x %.16s %s%s", m->module.type, m->module.name, oh_app_module_source(),
             oh_module_switches_mains(m) ? " MAINS" : "");
    oh_fb_text(&s.fb, 0, y, 1, m->source == OH_MOD_SRC_NONE ? bad : fg, bg, line);
    y += ROW_H + 2;

    int rows_left = (s.fb.h - y) / ROW_H - 1;
    for (int i = 0; i < m->n_ports && i < rows_left; i++) {
        const oh_port_desc_t *p = &m->ports[i];
        char vbuf[24];
        const char *v = value_str(p, &st[i].value, vbuf, sizeof(vbuf));
        snprintf(line, sizeof(line), "%2u %c %-*.*s", p->port, oh_port_is_input(p) ? '<' : '>', cols - 5 - 8,
                 cols - 5 - 8, p->name);
        oh_fb_text(&s.fb, 0, y, 1, fg, bg, line);
        uint16_t vc = (p->type == OH_DT_BOOLEAN && st[i].value.state) ? ok : fg;
        oh_fb_text(&s.fb, s.fb.w - (int)strlen(v) * OH_FONT_W, y, 1, vc, bg, v);
        y += ROW_H;
    }
    if (m->n_ports > rows_left) {
        snprintf(line, sizeof(line), "+%d more", m->n_ports - rows_left);
        oh_fb_text(&s.fb, 0, y, 1, dim, bg, line);
    }
    oh_node_unlock();

    /* Bottom line: errors, or the clock. */
    char codes[OH_MAX_ERRORS][OH_ERROR_CODE_MAX];
    size_t n = oh_node_errors(codes, OH_MAX_ERRORS);
    y = s.fb.h - ROW_H;
    if (n) {
        line[0] = '\0';
        for (size_t i = 0; i < n; i++) {
            strncat(line, codes[i], sizeof(line) - strlen(line) - 1);
            strncat(line, " ", sizeof(line) - strlen(line) - 1);
        }
        oh_fb_text(&s.fb, 0, y, 1, bad, bg, line);
    } else {
        uint32_t up = oh_clock_uptime_s();
        snprintf(line, sizeof(line), "up %lu:%02lu:%02lu %s", (unsigned long)up / 3600, (unsigned long)(up / 60) % 60,
                 (unsigned long)up % 60, oh_clock_is_unix() ? "ntp" : "");
        oh_fb_text(&s.fb, 0, y, 1, dim, bg, line);
    }
}

/* ── Task ─────────────────────────────────────────────────────────────── */

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        int64_t now = esp_timer_get_time();
        bool identifying = now < s.identify_until_us;
        TickType_t wait = pdMS_TO_TICKS(identifying ? 125 : 1000);
        EventBits_t bits = xEventGroupWaitBits(s.events, DIRTY_BIT, pdTRUE, pdFALSE, wait);
        now = esp_timer_get_time();
        if (now < s.identify_until_us) {
            /* Identify: flip the inversion at 4 Hz. Cheap, and unmissable. */
            s.inverted = !s.inverted;
            esp_lcd_panel_invert_color(s.panel, s.inverted);
            continue;
        }
#ifdef CONFIG_OH_DISPLAY_INVERT
        bool normal = true;
#else
        bool normal = false;
#endif
        if (s.inverted != normal) {
            s.inverted = normal;
            esp_lcd_panel_invert_color(s.panel, normal);
        }
        (void)bits;
        render();
        flush();
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == OH_EVENT_IDENTIFY) {
        s.identify_until_us = esp_timer_get_time() + (int64_t)(*(uint32_t *)data) * 1000;
    }
    xEventGroupSetBits(s.events, DIRTY_BIT);
}

int oh_display_rotation(void)
{
    return s.rotation;
}

esp_err_t oh_display_set_rotation(int rotation)
{
    s.rotation = rotation & 3;
    nvs_handle_t h;
    if (nvs_open("ohdisp", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "rot", (uint8_t)s.rotation);
        nvs_commit(h);
        nvs_close(h);
    }
    if (s.panel) {
        apply_rotation();
        oh_fb_fill(&s.fb, 0, 0, s.fb.w, s.fb.h, 0);
        xEventGroupSetBits(s.events, DIRTY_BIT);
    }
    return ESP_OK;
}

esp_err_t oh_display_start(void)
{
    nvs_handle_t h;
    uint8_t rot = 0;
    if (nvs_open("ohdisp", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "rot", &rot);
        nvs_close(h);
    }
    s.rotation = rot & 3;
    size_t px = (size_t)CONFIG_OH_DISPLAY_WIDTH * CONFIG_OH_DISPLAY_HEIGHT;
    s.fb.px = heap_caps_calloc(px, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s.fb.px) {
        s.fb.px = heap_caps_calloc(px, sizeof(uint16_t), MALLOC_CAP_DMA);
    }
    if (!s.fb.px) {
        ESP_LOGE(TAG, "no memory for a %ux%u framebuffer", CONFIG_OH_DISPLAY_WIDTH, CONFIG_OH_DISPLAY_HEIGHT);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = panel_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel: %s", esp_err_to_name(err));
        return err;
    }
    s.events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(OH_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    xEventGroupSetBits(s.events, DIRTY_BIT);
    if (xTaskCreate(task, "oh_display", 6144, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "%dx%d, rotation %d", s.fb.w, s.fb.h, s.rotation);
    return ESP_OK;
}
