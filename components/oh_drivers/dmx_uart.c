/*
 * dmx_uart: the DMX512 output module (0x0001). Whatever sACN delivers for
 * the configured universe goes out the module UART at 250 kbaud, 8N2, with
 * a break before every frame, refreshed continuously so a fixture that
 * missed a frame is not left holding the last one forever.
 *
 *   { "driver": "dmx_uart", "ports": [], "uart": 1, "tx": 17, "de": 4, "refresh_hz": 40 }
 *
 * The module has no ports of its own: the universe is the interface.
 */
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "oh_driver.h"
#include "oh_sacn.h"

static const char *TAG = "dmx_uart";

typedef struct {
    int uart;
    uint8_t frame[513]; /* start code + 512 slots */
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    volatile bool stop;
    uint32_t period_ms;
    bool installed;
} inst_t;

static void on_frame(void *ctx, const oh_e131_frame_t *f)
{
    inst_t *i = ctx;
    xSemaphoreTake(i->lock, portMAX_DELAY);
    memset(i->frame + 1, 0, 512);
    memcpy(i->frame + 1, f->slots, f->n_slots > 512 ? 512 : f->n_slots);
    xSemaphoreGive(i->lock);
}

static void task(void *arg)
{
    inst_t *i = arg;
    uint8_t out[513];
    while (!i->stop) {
        xSemaphoreTake(i->lock, portMAX_DELAY);
        memcpy(out, i->frame, sizeof(out));
        xSemaphoreGive(i->lock);
        /* The break goes out after these bytes, i.e. before the next frame;
         * the gap until the next write is the mark-after-break. 25 bit times
         * at 250 kbaud is a 100 us break. */
        uart_write_bytes_with_break(i->uart, out, sizeof(out), 25);
        uart_wait_tx_done(i->uart, pdMS_TO_TICKS(50));
        vTaskDelay(pdMS_TO_TICKS(i->period_ms));
    }
    i->task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t init(const oh_driver_bind_t *bind, void **out)
{
    int tx = oh_driver_param_int(bind->params, "tx", -1);
    int de = oh_driver_param_int(bind->params, "de", -1);
    int rx = oh_driver_param_int(bind->params, "rx", -1);
    if (tx < 0) {
        ESP_LOGE(TAG, "no tx pin on this board");
        return ESP_ERR_INVALID_ARG;
    }
    inst_t *i = calloc(1, sizeof(*i));
    if (!i) {
        return ESP_ERR_NO_MEM;
    }
    i->uart = oh_driver_param_int(bind->params, "uart", 1);
    int hz = oh_driver_param_int(bind->params, "refresh_hz", 40);
    i->period_ms = hz > 0 ? (uint32_t)(1000 / hz) : 25;
    i->lock = xSemaphoreCreateMutex();
    uart_config_t cfg = {
        .baud_rate = 250000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(i->uart, 256, 1024, 0, NULL, 0);
    if (err == ESP_OK) {
        i->installed = true;
        err = uart_param_config(i->uart, &cfg);
    }
    if (err == ESP_OK) {
        err = uart_set_pin(i->uart, tx, rx, de, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK && de >= 0) {
        /* RTS doubles as the transceiver's driver enable. */
        err = uart_set_mode(i->uart, UART_MODE_RS485_HALF_DUPLEX);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart %d: %s", i->uart, esp_err_to_name(err));
        if (i->installed) {
            uart_driver_delete(i->uart);
        }
        vSemaphoreDelete(i->lock);
        free(i);
        return err;
    }
    oh_sacn_set_sink(on_frame, i);
    xTaskCreate(task, "dmx_uart", 3072, i, 9, &i->task);
    ESP_LOGI(TAG, "uart %d tx %d de %d, %d Hz", i->uart, tx, de, hz);
    *out = i;
    return ESP_OK;
}

static void deinit(void *arg)
{
    inst_t *i = arg;
    oh_sacn_set_sink(NULL, NULL);
    i->stop = true;
    for (int n = 0; n < 50 && i->task; n++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    uart_driver_delete(i->uart);
    vSemaphoreDelete(i->lock);
    free(i);
}

const oh_driver_t oh_driver_dmx_uart = {.id = "dmx_uart", .init = init, .read = NULL, .write = NULL, .deinit = deinit};
