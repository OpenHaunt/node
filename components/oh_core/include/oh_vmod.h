/*
 * A virtual module: a JSON file that says what this node is, in the same
 * shape as the-pult's node-sim `NodeConfig`, plus `bindings` that tie ports
 * to drivers and pins. Lets a devkit without an ID EEPROM pose as any
 * module, and lets a module nobody has built run.
 *
 *   {
 *     "name": "Bench node",
 *     "module": { "type": "0x0006", "name": "Dry Contacts", "rev": "a", "flags": 0, "caps": "" },
 *     "ports": [ { "port": 0, "name": "Contact 1", "access": "readwrite", "dataType": "boolean",
 *                  "default": 0, "class": "switch" } ],
 *     "dmx": { "protocols": ["sacn"], "universes": 1 },
 *     "bindings": [ { "driver": "gpio_out", "port": 0, "gpio": 18 },
 *                   { "driver": "mcp23017_in", "ports": [1,2,3], "addr": 32 } ]
 *   }
 */
#pragma once

#include "oh_json.h"
#include "oh_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse and validate. On failure `err` (OH_ERR_MAX) says what is wrong. */
bool oh_vmod_parse(const char *json, oh_module_t *out, char *err);
bool oh_vmod_parse_object(const cJSON *root, oh_module_t *out, char *err);

/* The module back as JSON, in the same shape (bindings included). */
cJSON *oh_vmod_to_json(const oh_module_t *m);

/* Pins a preset may bind to on this board. -1 = none. */
typedef struct {
    int inputs[8];  /* free GPIOs for contacts, first is usually the BOOT button */
    int outputs[4]; /* free GPIOs for switches */
    int strip;      /* MOD_FAST0 or the board's strip pin */
    int mod_int;    /* MCP23017 interrupt */
} oh_vmod_pins_t;

/* The catalogue modules as somewhere to start from, mirroring the sim's
 * presets: input, led, relay, oled, contact, env, dmx. */
bool oh_vmod_preset(const char *key, const oh_vmod_pins_t *pins, oh_module_t *out);
const char *const *oh_vmod_preset_keys(size_t *n);

#ifdef __cplusplus
}
#endif
