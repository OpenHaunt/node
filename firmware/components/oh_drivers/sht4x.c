/*
 * sht4x: Sensirion SHT40/41/45 temperature and humidity over I2C, for the
 * environment module (0x0007). Polled on a timer; a reading is published
 * when it moves by more than the hysteresis, and at least every 30 s.
 *
 *   { "driver": "sht4x", "ports": [0, 1], "addr": 68, "interval_ms": 2000,
 *     "hyst_c": 0.2, "hyst_rh": 1.0, "max_silence_ms": 30000 }
 *
 * Which port is which follows the description: the port whose unit is
 * degree-celsius (or class temperature) gets the temperature, the other the
 * humidity.
 */
#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oh_driver.h"
#include "oh_i2c.h"

static const char *TAG = "sht4x";

typedef struct {
    i2c_master_dev_handle_t dev;
    int addr;
    int temp_local, hum_local;
    const oh_port_desc_t *ports[OH_MAX_PORTS];
    uint32_t interval_ms, max_silence_ms;
    float hyst_c, hyst_rh;
    float last_t, last_rh;
    int64_t last_t_us, last_rh_us;
    oh_input_cb_t cb;
    void *cb_ctx;
    TaskHandle_t task;
    volatile bool stop;
} inst_t;

static uint8_t crc8(const uint8_t *d, size_t n)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t measure(inst_t *i, float *t, float *rh)
{
    uint8_t cmd = 0xFD; /* high repeatability */
    esp_err_t err = i2c_master_transmit(i->dev, &cmd, 1, 50);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(12));
    uint8_t d[6];
    err = i2c_master_receive(i->dev, d, 6, 50);
    if (err != ESP_OK) {
        return err;
    }
    if (crc8(d, 2) != d[2] || crc8(d + 3, 2) != d[5]) {
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t rt = (uint16_t)((d[0] << 8) | d[1]), rrh = (uint16_t)((d[3] << 8) | d[4]);
    *t = -45.0f + 175.0f * rt / 65535.0f;
    *rh = -6.0f + 125.0f * rrh / 65535.0f;
    if (*rh < 0) {
        *rh = 0;
    }
    if (*rh > 100) {
        *rh = 100;
    }
    return ESP_OK;
}

static void report(inst_t *i, int local, float value, float *last, int64_t *last_us, float hyst, int64_t now)
{
    if (local < 0 || !i->ports[local]) {
        return;
    }
    bool due = fabsf(value - *last) >= hyst || (now - *last_us) >= (int64_t)i->max_silence_ms * 1000 ||
               *last_us == 0;
    if (!due) {
        return;
    }
    *last = value;
    *last_us = now;
    oh_value_t v = {.type = OH_DT_NUMBER, .value = value};
    i->cb(i->cb_ctx, i->ports[local]->port, &v, now);
}

static void task(void *arg)
{
    inst_t *i = arg;
    int failures = 0;
    while (!i->stop) {
        float t, rh;
        int64_t now = esp_timer_get_time();
        if (measure(i, &t, &rh) == ESP_OK) {
            failures = 0;
            report(i, i->temp_local, t, &i->last_t, &i->last_t_us, i->hyst_c, now);
            report(i, i->hum_local, rh, &i->last_rh, &i->last_rh_us, i->hyst_rh, now);
        } else if (++failures == 3) {
            ESP_LOGW(TAG, "sensor at 0x%02x stopped answering", i->addr);
        }
        vTaskDelay(pdMS_TO_TICKS(i->interval_ms));
    }
    i->task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t init(const oh_driver_bind_t *bind, void **out)
{
    inst_t *i = calloc(1, sizeof(*i));
    if (!i) {
        return ESP_ERR_NO_MEM;
    }
    i->addr = oh_driver_param_int(bind->params, "addr", 0x44);
    i->interval_ms = (uint32_t)oh_driver_param_int(bind->params, "interval_ms", 2000);
    i->max_silence_ms = (uint32_t)oh_driver_param_int(bind->params, "max_silence_ms", 30000);
    const cJSON *h;
    i->hyst_c = cJSON_IsNumber(h = cJSON_GetObjectItemCaseSensitive(bind->params, "hyst_c")) ? (float)h->valuedouble : 0.2f;
    i->hyst_rh = cJSON_IsNumber(h = cJSON_GetObjectItemCaseSensitive(bind->params, "hyst_rh")) ? (float)h->valuedouble : 1.0f;
    i->temp_local = i->hum_local = -1;
    for (int n = 0; n < bind->n_ports; n++) {
        const oh_port_desc_t *p = bind->ports[n];
        i->ports[n] = p;
        if (!p) {
            continue;
        }
        if (strcmp(p->class_, "temperature") == 0 || strcmp(p->unit, "degree-celsius") == 0) {
            i->temp_local = n;
        } else if (strcmp(p->class_, "humidity") == 0 || strcmp(p->unit, "percent") == 0) {
            i->hum_local = n;
        }
    }
    i->cb = bind->input;
    i->cb_ctx = bind->input_ctx;
    esp_err_t err = oh_i2c_device(i->addr, 100000, &i->dev);
    if (err != ESP_OK) {
        free(i);
        return err;
    }
    float t, rh;
    err = measure(i, &t, &rh);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no sensor at 0x%02x (%s)", i->addr, esp_err_to_name(err));
        i2c_master_bus_rm_device(i->dev);
        free(i);
        return err;
    }
    i->last_t = t;
    i->last_rh = rh;
    xTaskCreate(task, "sht4x", 3072, i, 5, &i->task);
    ESP_LOGI(TAG, "sensor at 0x%02x: %.1f C, %.0f %%RH", i->addr, t, rh);
    *out = i;
    return ESP_OK;
}

static esp_err_t read(void *arg, uint8_t local, oh_value_t *out)
{
    inst_t *i = arg;
    out->type = OH_DT_NUMBER;
    if ((int)local == i->temp_local) {
        out->value = i->last_t;
    } else if ((int)local == i->hum_local) {
        out->value = i->last_rh;
    } else {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static void deinit(void *arg)
{
    inst_t *i = arg;
    i->stop = true;
    for (int n = 0; n < 100 && i->task; n++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    i2c_master_bus_rm_device(i->dev);
    free(i);
}

const oh_driver_t oh_driver_sht4x = {.id = "sht4x", .init = init, .read = read, .write = NULL, .deinit = deinit};
