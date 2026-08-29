/*
 * ws2812: an addressable strip on an RMT pin. Two ports — a colour and an
 * intensity — and every pixel shows colour × intensity.
 *
 *   { "driver": "ws2812", "ports": [0, 1], "gpio": 4, "count": 8, "order": "GRB" }
 */
#include <string.h>

#include "esp_log.h"
#include "led_strip.h"
#include "oh_driver.h"

static const char *TAG = "ws2812";

typedef struct {
    led_strip_handle_t strip;
    int count;
    int colour_local, intensity_local; /* -1 when the module has no such port */
    oh_color_t colour;
    float intensity;
} inst_t;

static void show(inst_t *i)
{
    float k = i->intensity < 0 ? 0 : i->intensity > 1 ? 1 : i->intensity;
    uint8_t r = (uint8_t)(i->colour.r * k + 0.5f), g = (uint8_t)(i->colour.g * k + 0.5f),
            b = (uint8_t)(i->colour.b * k + 0.5f);
    for (int n = 0; n < i->count; n++) {
        led_strip_set_pixel(i->strip, n, r, g, b);
    }
    led_strip_refresh(i->strip);
}

static esp_err_t init(const oh_driver_bind_t *bind, void **out)
{
    int gpio = oh_driver_param_int(bind->params, "gpio", -1);
    if (gpio < 0) {
        ESP_LOGE(TAG, "no gpio");
        return ESP_ERR_INVALID_ARG;
    }
    inst_t *i = calloc(1, sizeof(*i));
    if (!i) {
        return ESP_ERR_NO_MEM;
    }
    i->count = oh_driver_param_int(bind->params, "count", 8);
    if (i->count < 1) {
        i->count = 1;
    }
    i->colour_local = i->intensity_local = -1;
    for (int n = 0; n < bind->n_ports; n++) {
        if (!bind->ports[n]) {
            continue;
        }
        if (bind->ports[n]->type == OH_DT_COLOR) {
            i->colour_local = n;
        } else if (bind->ports[n]->type == OH_DT_NUMBER) {
            i->intensity_local = n;
            i->intensity = bind->ports[n]->has_default ? bind->ports[n]->default_value : 0;
        }
    }
    /* A strip with no intensity port is driven by its colour alone. */
    if (i->intensity_local < 0) {
        i->intensity = 1.0f;
    }
    const char *order = oh_driver_param_str(bind->params, "order", "GRB");
    led_strip_config_t strip = {
        .strip_gpio_num = gpio,
        .max_leds = i->count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = strcmp(order, "RGB") == 0 ? LED_STRIP_COLOR_COMPONENT_FMT_RGB
                                                            : LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = oh_driver_param_bool(bind->params, "invert", false),
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
#if SOC_RMT_SUPPORT_DMA
        .flags.with_dma = i->count > 64,
#endif
    };
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &i->strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio %d: %s", gpio, esp_err_to_name(err));
        free(i);
        return err;
    }
    show(i);
    ESP_LOGI(TAG, "%d pixels on gpio %d (%s)", i->count, gpio, order);
    *out = i;
    return ESP_OK;
}

static esp_err_t write(void *arg, uint8_t local, const oh_value_t *in)
{
    inst_t *i = arg;
    if ((int)local == i->colour_local && in->type == OH_DT_COLOR) {
        i->colour = in->color;
    } else if ((int)local == i->intensity_local && in->type == OH_DT_NUMBER) {
        i->intensity = in->value;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    show(i);
    return ESP_OK;
}

static esp_err_t read(void *arg, uint8_t local, oh_value_t *out)
{
    inst_t *i = arg;
    if ((int)local == i->colour_local) {
        out->type = OH_DT_COLOR;
        out->color = i->colour;
    } else if ((int)local == i->intensity_local) {
        out->type = OH_DT_NUMBER;
        out->value = i->intensity;
    } else {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static void deinit(void *arg)
{
    inst_t *i = arg;
    led_strip_clear(i->strip);
    led_strip_del(i->strip);
    free(i);
}

const oh_driver_t oh_driver_ws2812 = {
    .id = "ws2812",
    .init = init,
    .read = read,
    .write = write,
    .deinit = deinit,
};
