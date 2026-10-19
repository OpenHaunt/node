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

/* Re-publish the TXT record from the current module and name. */
void oh_app_announce(void);

void oh_app_reboot(uint32_t delay_ms);
void oh_app_factory_reset(void);

#ifdef __cplusplus
}
#endif
