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
#include "oh_pixel.h"

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
    int uart_tx, uart_rx, uart_de; /* the module UART, for the DMX gateway */
} oh_vmod_pins_t;

/* The catalogue modules as somewhere to start from, mirroring the sim's
 * presets: input, led, relay, oled, contact, env, dmx. */
bool oh_vmod_preset(const char *key, const oh_vmod_pins_t *pins, oh_module_t *out);
const char *const *oh_vmod_preset_keys(size_t *n);

/*
 * A board whose I/O is soldered on rather than plugged in: the V0 prototype has
 * four relays, two sensor terminals and two strip outputs and no module
 * connector at all, so there is no EEPROM to read and nothing for a preset to
 * pose as. What it is follows from the board, and this is that description.
 * A stored virtual module still wins over it, so a terminal can be rebound.
 */
#define OH_MODULE_TYPE_NODE_V0 0x0F01

typedef struct {
    int relays[4];          /* -1 = not fitted */
    int inputs[2];
    /* What the board does to a contact closed to GND on the terminal. V0.1
     * pulls the GPIO up through the terminal, so a closed contact reads low;
     * V0.2 puts a transistor in the way, so it reads high. Either way the port
     * reads true for a closed contact, and both revisions describe the same. */
    bool input_contact_low;
    int strips[2];
    int strip_count;        /* pixels driven per strip when its settings name none */
    /* Each strip's mode and settings (doc 14), as stored on the node. A `count`
     * of zero is "nothing stored": the defaults, at `strip_count` pixels. */
    oh_pix_cfg_t strip_cfg[2];
    const char *rev;        /* "0.1", "0.2" */
} oh_vmod_board_io_t;

/* Where strip `n`'s nine effect ports start on the V0 (doc 14): 6 and 15. */
#define OH_V0_STRIP_PORT_BASE(n) (6 + 9 * (n))
#define OH_V0_STRIP_PORTS 9

/* Strip `n`'s settings as the builder reads them: `strip_cfg[n]`, or the
 * defaults at `strip_count` pixels when nothing is stored. */
oh_pix_cfg_t oh_vmod_board_strip_cfg(const oh_vmod_board_io_t *io, int n);

bool oh_vmod_board_v0(const oh_vmod_board_io_t *io, oh_module_t *out);

#ifdef __cplusplus
}
#endif
