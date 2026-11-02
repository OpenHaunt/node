/*
 * gpio_out: a switch on a plain GPIO.
 *
 *   { "driver": "gpio_out", "port": 0, "gpio": 18, "invert": false, "open_drain": false }
 */
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "oh_driver.h"

static const char *TAG = "gpio_out";

typedef struct {
    int gpio;
    bool invert;
    bool state;
} pin_t;

typedef struct {
    pin_t pins[OH_MAX_PORTS];
    uint8_t n;
} inst_t;

static esp_err_t init(const oh_driver_bind_t *bind, void **out)
{
    inst_t *inst = calloc(1, sizeof(*inst));
    if (!inst) {
        return ESP_ERR_NO_MEM;
    }
    const cJSON *gpios = cJSON_GetObjectItemCaseSensitive(bind->params, "gpios");
    bool open_drain = oh_driver_param_bool(bind->params, "open_drain", false);
    for (uint8_t i = 0; i < bind->n_ports; i++) {
        int gpio = cJSON_IsArray(gpios) && cJSON_GetArrayItem(gpios, i)
                       ? cJSON_GetArrayItem(gpios, i)->valueint
                       : oh_driver_param_int(bind->params, "gpio", -1);
        if (gpio < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
            ESP_LOGE(TAG, "local port %u: no valid output gpio", i);
            free(inst);
            return ESP_ERR_INVALID_ARG;
        }
        pin_t *pin = &inst->pins[i];
        pin->gpio = gpio;
        pin->invert = oh_driver_param_bool(bind->params, "invert", false);
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << gpio,
            .mode = open_drain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK) {
            free(inst);
            return err;
        }
        gpio_set_level(gpio, pin->invert ? 1 : 0);
        inst->n++;
        ESP_LOGI(TAG, "port %u on gpio %d%s", bind->ports[i] ? bind->ports[i]->port : i, gpio,
                 pin->invert ? " (inverted)" : "");
    }
    *out = inst;
    return ESP_OK;
}

static esp_err_t write(void *arg, uint8_t local, const oh_value_t *in)
{
    inst_t *inst = arg;
    if (local >= inst->n) {
        return ESP_ERR_NOT_FOUND;
    }
    if (in->type != OH_DT_BOOLEAN) {
        return ESP_ERR_INVALID_ARG;
    }
    pin_t *pin = &inst->pins[local];
    pin->state = in->state;
    return gpio_set_level(pin->gpio, in->state != pin->invert);
}

static esp_err_t read(void *arg, uint8_t local, oh_value_t *out)
{
    inst_t *inst = arg;
    if (local >= inst->n) {
        return ESP_ERR_NOT_FOUND;
    }
    out->type = OH_DT_BOOLEAN;
    out->state = inst->pins[local].state;
    return ESP_OK;
}

static void deinit(void *arg)
{
    inst_t *inst = arg;
    for (int i = 0; i < inst->n; i++) {
        gpio_set_level(inst->pins[i].gpio, inst->pins[i].invert ? 1 : 0);
        gpio_reset_pin(inst->pins[i].gpio);
    }
    free(inst);
}

const oh_driver_t oh_driver_gpio_out = {
    .id = "gpio_out",
    .init = init,
    .read = read,
    .write = write,
    .deinit = deinit,
};
