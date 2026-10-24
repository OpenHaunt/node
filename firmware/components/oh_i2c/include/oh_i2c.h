/* The module I2C bus, created once from the board's pins and shared by the
 * ID EEPROM and every I2C driver. */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oh_i2c_bus(i2c_master_bus_handle_t *out);
/* Add a device at `addr`; the caller owns the handle. */
esp_err_t oh_i2c_device(uint16_t addr, uint32_t speed_hz, i2c_master_dev_handle_t *out);
/* True when something answers at `addr`. */
bool oh_i2c_probe(uint16_t addr);

#ifdef __cplusplus
}
#endif
