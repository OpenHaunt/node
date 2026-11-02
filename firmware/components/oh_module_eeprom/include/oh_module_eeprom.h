/*
 * The plug-on module's identity: a 24AA02UID at I2C 0x50 holding the doc-04
 * descriptor, and MOD_nRESET, which the base board holds low until the
 * module is known and within budget.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "oh_descriptor.h"
#include "oh_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hold the module in reset (boot) / release it (after bind). */
void oh_module_reset_hold(void);
void oh_module_reset_release(void);

/* True when something answers at 0x50. */
bool oh_eeprom_present(void);
esp_err_t oh_eeprom_read(uint8_t raw[OH_EEPROM_SIZE]);
/* Program the descriptor (with CRC) into 0x00..0x31. The UID area is
 * read-only on a 24AA02UID; on a plain 24C02 it stays whatever it was. */
esp_err_t oh_eeprom_write_descriptor(const oh_eeprom_descriptor_t *d);

/* What the firmware knows about a module type: its ports and the drivers
 * behind them, from OpenHaunt/node doc 05. Returns false for a type this
 * build has no driver for; the module is then reported but has no ports. */
bool oh_module_table_build(uint16_t type, oh_module_t *out);

/* Read, validate and describe the plugged-in module. Returns ESP_ERR_NOT_FOUND
 * when no EEPROM answers, ESP_ERR_INVALID_CRC / _VERSION for a bad one
 * (`result` says which), ESP_ERR_INVALID_SIZE when its declared draw exceeds
 * the budget (the module is described but must not be enabled). */
esp_err_t oh_eeprom_describe(oh_module_t *out, oh_desc_result_t *result, uint32_t *power_mw);

#ifdef __cplusplus
}
#endif
