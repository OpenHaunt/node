/*
 * Everything a node is, in one value: identity, module descriptor, the ports
 * it describes, the universe it forwards (if any), and how each port is bound
 * to a driver. This is the same shape as the-pult's node-sim `NodeConfig`
 * with `bindings` added, and it is what the EEPROM path and the virtual
 * module path both produce.
 */
#pragma once

#include "oh_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OH_MODSN_MAX 24
#define OH_REV_MAX 8
#define OH_CAPS_MAX 32
#define OH_DRIVER_ID_MAX 16
#define OH_MAX_BINDINGS 8
#define OH_BINDING_PARAMS_MAX 192
#define OH_DMX_PROTOCOLS_MAX 2
#define OH_PROTOCOL_MAX 8

/* Descriptor capability bit 6: the module switches mains voltage. */
#define OH_MODULE_FLAG_MAINS (1u << 6)
/* The other capability bits from doc 04. */
#define OH_MODULE_FLAG_UART_TX (1u << 0)
#define OH_MODULE_FLAG_UART_RX (1u << 1)
#define OH_MODULE_FLAG_UART_DE (1u << 2)
#define OH_MODULE_FLAG_FAST0 (1u << 3)
#define OH_MODULE_FLAG_INT (1u << 4)
#define OH_MODULE_FLAG_I2C_EXTRA (1u << 5)
#define OH_MODULE_FLAG_ISOLATED (1u << 7)

typedef struct {
    uint16_t type;             /* 0x0003 on the wire */
    char name[OH_NAME_MAX];    /* "WS2812" */
    char sn[OH_MODSN_MAX];     /* module serial (EEPROM UID), or "mod-<sn>" */
    char rev[OH_REV_MAX];      /* "2", "a" */
    uint32_t flags;            /* capability bitfield, verbatim */
    char caps[OH_CAPS_MAX];    /* "dmx,rdm,sacn" */
} oh_module_desc_t;

typedef struct {
    char protocols[OH_DMX_PROTOCOLS_MAX][OH_PROTOCOL_MAX];
    uint8_t n_protocols;
    uint16_t universes;
} oh_dmx_desc_t;

/* One driver instance and the ports it owns. `ports[i]` is the global port
 * number of the driver's local port i. The driver-specific parameters stay
 * as JSON text so that oh_core does not have to know every driver's keys. */
typedef struct {
    char driver[OH_DRIVER_ID_MAX];
    uint8_t ports[OH_MAX_PORTS];
    uint8_t n_ports;
    char params_json[OH_BINDING_PARAMS_MAX];
} oh_binding_t;

typedef enum {
    OH_MOD_SRC_NONE = 0, /* headless: no module found, nothing configured */
    OH_MOD_SRC_EEPROM,
    OH_MOD_SRC_VIRTUAL,
} oh_module_source_t;

typedef struct {
    char name[OH_NAME_MAX]; /* friendly name; "" means "<module name> <sn>" */
    oh_module_desc_t module;
    oh_port_desc_t ports[OH_MAX_PORTS];
    uint8_t n_ports;
    bool has_dmx;
    oh_dmx_desc_t dmx;
    oh_binding_t bindings[OH_MAX_BINDINGS];
    uint8_t n_bindings;
    oh_module_source_t source;
} oh_module_t;

/* A node with no module: type 0x0000, no ports. */
void oh_module_init_headless(oh_module_t *m);

const oh_port_desc_t *oh_module_find_port(const oh_module_t *m, uint8_t port);
int oh_module_port_index(const oh_module_t *m, uint8_t port);

/* Append a port; returns false when full or when the number is taken. */
bool oh_module_add_port(oh_module_t *m, const oh_port_desc_t *p);

/* Append a binding; returns false when full. */
bool oh_module_add_binding(oh_module_t *m, const char *driver, const uint8_t *ports, uint8_t n_ports,
                           const char *params_json);

bool oh_module_switches_mains(const oh_module_t *m);
bool oh_module_dmx_accepts(const oh_module_t *m, const char *protocol);

/* True when the module says nothing a controller could patch. */
bool oh_module_is_empty(const oh_module_t *m);

#ifdef __cplusplus
}
#endif
