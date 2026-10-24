/*
 * gpio_in: a contact on a plain GPIO. Any-edge interrupt, the timestamp taken
 * in the ISR, debounced in one shared task so the event carries the moment
 * of the first edge rather than the moment it settled.
 *
 *   { "driver": "gpio_in", "port": 1, "gpio": 9, "pull": "up", "invert": true, "debounce_ms": 20 }
 */
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "oh_debounce.h"
#include "oh_driver.h"

static const char *TAG = "gpio_in";

typedef struct pin {
    int gpio;
    bool invert;
    uint8_t port;
    oh_debounce_t db;
    oh_input_cb_t cb;
    void *cb_ctx;
    bool active;
} pin_t;

typedef struct {
    pin_t pins[OH_MAX_PORTS];
    uint8_t n;
} inst_t;

typedef struct {
    pin_t *pin;
    bool level;
    int64_t ts_us;
} edge_t;

#define MAX_PINS 32

static QueueHandle_t s_edges;
static pin_t *s_all[MAX_PINS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR isr(void *arg)
{
    pin_t *pin = arg;
    edge_t e = {.pin = pin, .level = gpio_get_level(pin->gpio), .ts_us = esp_timer_get_time()};
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_edges, &e, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void emit(pin_t *pin, bool level, int64_t ts_us)
{
    oh_value_t v = {.type = OH_DT_BOOLEAN, .state = level != pin->invert};
    pin->cb(pin->cb_ctx, pin->port, &v, ts_us);
}

static void feed(pin_t *pin, bool raw, int64_t now_us)
{
    bool level;
    int64_t ts;
    if (oh_debounce_feed(&pin->db, raw, now_us, &level, &ts)) {
        emit(pin, level, ts);
    }
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        /* Sleep until the next pending edge would settle, or an ISR speaks. */
        int64_t now = esp_timer_get_time();
        int64_t wait_us = 1000000;
        portENTER_CRITICAL(&s_mux);
        for (int i = 0; i < MAX_PINS; i++) {
            pin_t *p = s_all[i];
            if (p && p->active) {
                int64_t due = oh_debounce_due(&p->db);
                if (due >= 0 && due - now < wait_us) {
                    wait_us = due - now;
                }
            }
        }
        portEXIT_CRITICAL(&s_mux);
        if (wait_us < 0) {
            wait_us = 0;
        }
        edge_t e;
        TickType_t ticks = pdMS_TO_TICKS(wait_us / 1000) + 1;
        if (xQueueReceive(s_edges, &e, ticks) == pdTRUE) {
            if (e.pin->active) {
                feed(e.pin, e.level, e.ts_us);
            }
            continue;
        }
        /* Timer path: re-sample every pin with a pending edge. */
        now = esp_timer_get_time();
        for (int i = 0; i < MAX_PINS; i++) {
            pin_t *p = s_all[i];
            if (p && p->active && oh_debounce_due(&p->db) >= 0 && oh_debounce_due(&p->db) <= now) {
                feed(p, gpio_get_level(p->gpio), now);
            }
        }
    }
}

static esp_err_t service_start(void)
{
    if (s_edges) {
        return ESP_OK;
    }
    s_edges = xQueueCreate(32, sizeof(edge_t));
    if (!s_edges) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (xTaskCreate(task, "gpio_in", 3072, NULL, 12, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static bool track(pin_t *pin)
{
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < MAX_PINS; i++) {
        if (!s_all[i]) {
            s_all[i] = pin;
            portEXIT_CRITICAL(&s_mux);
            return true;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return false;
}

static void untrack(pin_t *pin)
{
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < MAX_PINS; i++) {
        if (s_all[i] == pin) {
            s_all[i] = NULL;
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

static esp_err_t init(const oh_driver_bind_t *bind, void **out)
{
    esp_err_t err = service_start();
    if (err != ESP_OK) {
        return err;
    }
    inst_t *inst = calloc(1, sizeof(*inst));
    if (!inst) {
        return ESP_ERR_NO_MEM;
    }
    /* One binding may name several ports with "gpios": [..]; the usual form is
     * one port and one "gpio". */
    const cJSON *gpios = cJSON_GetObjectItemCaseSensitive(bind->params, "gpios");
    for (uint8_t i = 0; i < bind->n_ports; i++) {
        if (!bind->ports[i]) {
            continue;
        }
        int gpio = cJSON_IsArray(gpios) && cJSON_GetArrayItem(gpios, i)
                       ? cJSON_GetArrayItem(gpios, i)->valueint
                       : oh_driver_param_int(bind->params, "gpio", -1);
        if (gpio < 0 || !GPIO_IS_VALID_GPIO(gpio)) {
            ESP_LOGE(TAG, "port %u: no valid gpio", bind->ports[i]->port);
            continue;
        }
        pin_t *pin = &inst->pins[inst->n];
        pin->gpio = gpio;
        pin->port = bind->ports[i]->port;
        pin->invert = oh_driver_param_bool(bind->params, "invert", false);
        pin->cb = bind->input;
        pin->cb_ctx = bind->input_ctx;
        const char *pull = oh_driver_param_str(bind->params, "pull", "none");
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = strcmp(pull, "up") == 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = strcmp(pull, "down") == 0 ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        err = gpio_config(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio %d: %s", gpio, esp_err_to_name(err));
            continue;
        }
        oh_debounce_init(&pin->db, gpio_get_level(gpio), oh_driver_param_int(bind->params, "debounce_ms", 20));
        if (!track(pin)) {
            ESP_LOGE(TAG, "too many gpio_in pins");
            continue;
        }
        pin->active = true;
        gpio_isr_handler_add(gpio, isr, pin);
        inst->n++;
        ESP_LOGI(TAG, "port %u on gpio %d (pull %s%s)", pin->port, gpio, pull, pin->invert ? ", inverted" : "");
    }
    *out = inst;
    return ESP_OK;
}

static esp_err_t read(void *arg, uint8_t local, oh_value_t *out)
{
    inst_t *inst = arg;
    if (local >= inst->n) {
        return ESP_ERR_NOT_FOUND;
    }
    pin_t *pin = &inst->pins[local];
    out->type = OH_DT_BOOLEAN;
    out->state = pin->db.stable != pin->invert;
    return ESP_OK;
}

static void deinit(void *arg)
{
    inst_t *inst = arg;
    for (int i = 0; i < inst->n; i++) {
        pin_t *pin = &inst->pins[i];
        pin->active = false;
        gpio_isr_handler_remove(pin->gpio);
        untrack(pin);
        gpio_reset_pin(pin->gpio);
    }
    /* Drain anything the ISR queued for pins that no longer exist. */
    edge_t e;
    while (xQueueReceive(s_edges, &e, 0) == pdTRUE) {
    }
    free(inst);
}

const oh_driver_t oh_driver_gpio_in = {
    .id = "gpio_in",
    .init = init,
    .read = read,
    .write = NULL,
    .deinit = deinit,
};
