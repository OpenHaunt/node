/*
 * mcp23017_in / mcp23017_out: the I2C port expander behind the digital input
 * module (0x0002) and the dry contact module (0x0006). Inputs are
 * interrupt-driven through MOD_INT and stamped in the ISR.
 *
 *   { "driver": "mcp23017_in", "ports": [0,1,2,3,4,5,6,7], "addr": 32,
 *     "int_gpio": 36, "pins": [0,1,2,3,4,5,6,7], "invert": true, "pullup": true }
 *   { "driver": "mcp23017_out", "ports": [0,1,2,3], "addr": 33, "pins": [8,9,10,11] }
 *
 * `pins` are expander pins 0..15 (A0..A7, B0..B7), one per local port.
 */
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "oh_driver.h"
#include "oh_i2c.h"

static const char *TAG = "mcp23017";

enum {
    IODIR = 0x00, IPOL = 0x02, GPINTEN = 0x04, DEFVAL = 0x06, INTCON = 0x08, IOCON = 0x0A,
    GPPU = 0x0C, INTF = 0x0E, INTCAP = 0x10, GPIO_ = 0x12, OLAT = 0x14
};

typedef struct {
    i2c_master_dev_handle_t dev;
    int addr;
    int int_gpio;
    bool is_output;
    uint8_t n;
    uint8_t pin_of[OH_MAX_PORTS];                 /* local port -> expander pin */
    const oh_port_desc_t *ports[OH_MAX_PORTS];    /* stable for the life of the binding */
    uint16_t mask;                                /* pins in use */
    uint16_t invert;
    uint16_t last;                                /* last known GPIO/OLAT word */
    oh_input_cb_t cb;
    void *cb_ctx;
    SemaphoreHandle_t wake;
    volatile int64_t isr_ts;
    TaskHandle_t task;
    volatile bool stop;
} inst_t;

static esp_err_t reg_write16(inst_t *i, uint8_t reg, uint16_t v)
{
    uint8_t buf[3] = {reg, (uint8_t)v, (uint8_t)(v >> 8)};
    return i2c_master_transmit(i->dev, buf, 3, 50);
}

static esp_err_t reg_read16(inst_t *i, uint8_t reg, uint16_t *v)
{
    uint8_t out[2] = {0, 0};
    esp_err_t err = i2c_master_transmit_receive(i->dev, &reg, 1, out, 2, 50);
    *v = (uint16_t)(out[0] | (out[1] << 8));
    return err;
}

static bool level_of(inst_t *i, uint16_t word, int local)
{
    uint16_t bit = 1u << i->pin_of[local];
    return ((word & bit) != 0) != ((i->invert & bit) != 0);
}

static void IRAM_ATTR isr(void *arg)
{
    inst_t *i = arg;
    i->isr_ts = esp_timer_get_time();
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(i->wake, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void emit_changes(inst_t *i, uint16_t now, int64_t ts)
{
    uint16_t changed = (now ^ i->last) & i->mask;
    i->last = now;
    for (int local = 0; local < i->n && changed; local++) {
        uint16_t bit = 1u << i->pin_of[local];
        if ((changed & bit) && i->ports[local]) {
            oh_value_t v = {.type = OH_DT_BOOLEAN, .state = level_of(i, now, local)};
            i->cb(i->cb_ctx, i->ports[local]->port, &v, ts);
        }
    }
}

static void task(void *arg)
{
    inst_t *i = arg;
    while (!i->stop) {
        /* Wake on the interrupt, or every 500 ms to catch what a missed edge
         * or a bounce during the read left behind. */
        bool irq = xSemaphoreTake(i->wake, pdMS_TO_TICKS(500)) == pdTRUE;
        if (i->stop) {
            break;
        }
        int64_t ts = irq ? i->isr_ts : esp_timer_get_time();
        uint16_t word;
        if (irq && reg_read16(i, INTCAP, &word) == ESP_OK) {
            emit_changes(i, word, ts);
        }
        if (reg_read16(i, GPIO_, &word) == ESP_OK) {
            emit_changes(i, word, ts);
        }
    }
    i->task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t init_common(const oh_driver_bind_t *bind, bool output, void **out)
{
    inst_t *i = calloc(1, sizeof(*i));
    if (!i) {
        return ESP_ERR_NO_MEM;
    }
    i->addr = oh_driver_param_int(bind->params, "addr", 0x20);
    i->int_gpio = oh_driver_param_int(bind->params, "int_gpio", -1);
    i->is_output = output;
    i->n = bind->n_ports;
    i->cb = bind->input;
    i->cb_ctx = bind->input_ctx;
    const cJSON *pins = cJSON_GetObjectItemCaseSensitive(bind->params, "pins");
    for (int n = 0; n < i->n; n++) {
        const cJSON *p = cJSON_IsArray(pins) ? cJSON_GetArrayItem(pins, n) : NULL;
        i->pin_of[n] = (uint8_t)((cJSON_IsNumber(p) ? p->valueint : n) & 15);
        i->mask |= 1u << i->pin_of[n];
        i->ports[n] = bind->ports[n];
    }
    const cJSON *inv = cJSON_GetObjectItemCaseSensitive(bind->params, "invert");
    if (cJSON_IsBool(inv) && cJSON_IsTrue(inv)) {
        i->invert = i->mask;
    } else if (cJSON_IsNumber(inv)) {
        i->invert = (uint16_t)inv->valueint;
    }

    esp_err_t err = oh_i2c_device(i->addr, 400000, &i->dev);
    if (err != ESP_OK) {
        free(i);
        return err;
    }
    /* IOCON: MIRROR (one INT pin for both banks), ODR (open drain, active low). */
    uint8_t iocon[2] = {IOCON, 0x40 | 0x04};
    err = i2c_master_transmit(i->dev, iocon, 2, 50);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no expander at 0x%02x", i->addr);
        i2c_master_bus_rm_device(i->dev);
        free(i);
        return err;
    }
    if (output) {
        i->last = i->invert; /* everything off */
        reg_write16(i, OLAT, i->last);
        reg_write16(i, IODIR, (uint16_t)~i->mask);
    } else {
        reg_write16(i, IODIR, 0xFFFF);
        reg_write16(i, GPPU, oh_driver_param_bool(bind->params, "pullup", true) ? i->mask : 0);
        reg_write16(i, INTCON, 0);
        reg_write16(i, GPINTEN, i->mask);
        reg_read16(i, GPIO_, &i->last);
        i->wake = xSemaphoreCreateBinary();
        if (i->int_gpio >= 0) {
            gpio_config_t cfg = {.pin_bit_mask = 1ULL << i->int_gpio,
                                 .mode = GPIO_MODE_INPUT,
                                 .pull_up_en = GPIO_PULLUP_ENABLE,
                                 .intr_type = GPIO_INTR_NEGEDGE};
            gpio_config(&cfg);
            esp_err_t e = gpio_install_isr_service(0);
            if (e == ESP_OK || e == ESP_ERR_INVALID_STATE) {
                gpio_isr_handler_add(i->int_gpio, isr, i);
            }
        }
        xTaskCreate(task, "mcp23017", 3072, i, 12, &i->task);
    }
    ESP_LOGI(TAG, "%s at 0x%02x, %d pin(s), int gpio %d", output ? "outputs" : "inputs", i->addr, i->n, i->int_gpio);
    *out = i;
    return ESP_OK;
}

static esp_err_t init_in(const oh_driver_bind_t *bind, void **out)
{
    return init_common(bind, false, out);
}

static esp_err_t init_out(const oh_driver_bind_t *bind, void **out)
{
    return init_common(bind, true, out);
}

static esp_err_t read(void *arg, uint8_t local, oh_value_t *out)
{
    inst_t *i = arg;
    if (local >= i->n) {
        return ESP_ERR_NOT_FOUND;
    }
    out->type = OH_DT_BOOLEAN;
    out->state = level_of(i, i->last, local);
    return ESP_OK;
}

static esp_err_t write(void *arg, uint8_t local, const oh_value_t *in)
{
    inst_t *i = arg;
    if (!i->is_output || local >= i->n || in->type != OH_DT_BOOLEAN) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t bit = 1u << i->pin_of[local];
    bool level = in->state != ((i->invert & bit) != 0);
    uint16_t next = level ? (i->last | bit) : (i->last & ~bit);
    esp_err_t err = reg_write16(i, OLAT, next);
    if (err == ESP_OK) {
        i->last = next;
    }
    return err;
}

static void deinit(void *arg)
{
    inst_t *i = arg;
    if (i->int_gpio >= 0 && !i->is_output) {
        gpio_isr_handler_remove(i->int_gpio);
    }
    if (i->task) {
        i->stop = true;
        xSemaphoreGive(i->wake);
        for (int n = 0; n < 50 && i->task; n++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (i->is_output) {
        reg_write16(i, OLAT, i->invert);
    }
    i2c_master_bus_rm_device(i->dev);
    if (i->wake) {
        vSemaphoreDelete(i->wake);
    }
    free(i);
}

const oh_driver_t oh_driver_mcp23017_in = {
    .id = "mcp23017_in", .init = init_in, .read = read, .write = NULL, .deinit = deinit};
const oh_driver_t oh_driver_mcp23017_out = {
    .id = "mcp23017_out", .init = init_out, .read = read, .write = write, .deinit = deinit};
