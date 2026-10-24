#include "oh_i2c.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "oh_board.h"
#include "sdkconfig.h"

static const char *TAG = "oh_i2c";
static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_lock;

esp_err_t oh_i2c_bus(i2c_master_bus_handle_t *out)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (!s_bus) {
        const oh_board_pins_t *pins = oh_board_pins();
        if (pins->i2c_sda < 0 || pins->i2c_scl < 0) {
            err = ESP_ERR_NOT_SUPPORTED;
        } else {
            i2c_master_bus_config_t cfg = {
                .i2c_port = -1,
                .sda_io_num = pins->i2c_sda,
                .scl_io_num = pins->i2c_scl,
                .clk_source = I2C_CLK_SRC_DEFAULT,
                .glitch_ignore_cnt = 7,
                .flags.enable_internal_pullup = true,
            };
            err = i2c_new_master_bus(&cfg, &s_bus);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "bus on sda %d scl %d", pins->i2c_sda, pins->i2c_scl);
            } else {
                ESP_LOGE(TAG, "bus: %s", esp_err_to_name(err));
            }
        }
    }
    xSemaphoreGive(s_lock);
    *out = s_bus;
    return err;
}

esp_err_t oh_i2c_device(uint16_t addr, uint32_t speed_hz, i2c_master_dev_handle_t *out)
{
    i2c_master_bus_handle_t bus;
    esp_err_t err = oh_i2c_bus(&bus);
    if (err != ESP_OK) {
        return err;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed_hz ? speed_hz : CONFIG_OH_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(bus, &cfg, out);
}

bool oh_i2c_probe(uint16_t addr)
{
    i2c_master_bus_handle_t bus;
    if (oh_i2c_bus(&bus) != ESP_OK) {
        return false;
    }
    return i2c_master_probe(bus, addr, 50) == ESP_OK;
}
