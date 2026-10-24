/*
 * The coordinator: who this node is, which module it is being, and the
 * handful of actions the HTTP API, the console and the web UI all share.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "oh_json.h"
#include "oh_vmod.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Config store, node, drivers, module selection and bind. */
esp_err_t oh_app_init(void);
/* Transport, mDNS, SNTP. */
esp_err_t oh_app_start_network(void);

const char *oh_app_sn(void);       /* 8 hex from the eFuse MAC */
const char *oh_app_fw(void);       /* version.txt */
const char *oh_app_hostname(void); /* openhaunt-<sn> */
void oh_app_node_name(char *out, size_t cap);

/* EEPROM first, then the stored virtual module, else headless; then bind. */
esp_err_t oh_app_select_module(void);
esp_err_t oh_app_set_vmod_json(const char *json, char *err);
esp_err_t oh_app_set_preset(const char *key, char *err);
esp_err_t oh_app_clear_vmod(void);
const char *oh_app_module_source(void); /* "eeprom" | "virtual" | "board" | "none" */
oh_vmod_pins_t oh_app_preset_pins(void);
/* The board's built-in I/O, for boards that have it (oh_board_has_builtin_io). */
oh_vmod_board_io_t oh_app_board_io(void);

/* The `pixels` object of `info` (doc 14), with each strip's `status` when asked
 * for, or NULL when the node's strips are not the board's to describe. */
cJSON *oh_app_pixels_json(bool with_status);

/* `PUT /api/v1/pixels/<output>`: merge, validate, store, and describe the node
 * again. ESP_ERR_NOT_FOUND for no such output, ESP_ERR_INVALID_STATE while a
 * virtual module wins, ESP_ERR_INVALID_ARG with `err` for a bad setting. */
esp_err_t oh_app_set_pixels(int output, const cJSON *body, char *err, cJSON **strip_out);

/* Re-publish the TXT record from the current module and name. */
void oh_app_announce(void);

void oh_app_reboot(uint32_t delay_ms);
void oh_app_factory_reset(void);

#ifdef __cplusplus
}
#endif
