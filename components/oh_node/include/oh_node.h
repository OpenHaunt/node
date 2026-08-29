/*
 * The node: the module it is being, the ports' current values, the drivers
 * bound to them, and the events everything else listens to.
 */
#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "oh_driver.h"
#include "oh_module.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(OH_EVENT);

typedef enum {
    OH_EVENT_PORT_CHANGED,   /* oh_port_changed_t */
    OH_EVENT_MODULE_BOUND,   /* no data */
    OH_EVENT_IDENTIFY,       /* uint32_t duration ms */
    OH_EVENT_NET_UP,         /* no data */
    OH_EVENT_NET_DOWN,       /* no data */
    OH_EVENT_NET_SETUP_AP,   /* no data: the setup access point is open */
    OH_EVENT_MQTT_CONNECTED, /* no data */
    OH_EVENT_MQTT_DISCONNECTED,
    OH_EVENT_ERRORS_CHANGED, /* no data */
    OH_EVENT_CONFIG_CHANGED, /* uint32_t bitmask of OH_CFG_* */
    OH_EVENT_OTA_STARTED,
    OH_EVENT_OTA_FINISHED,
} oh_event_id_t;

typedef enum {
    OH_SOURCE_DRIVER = 0, /* an input the hardware produced */
    OH_SOURCE_HTTP,
    OH_SOURCE_MQTT,
    OH_SOURCE_CONSOLE,
    OH_SOURCE_INTERNAL,
} oh_source_t;

typedef struct {
    uint8_t port;
    oh_value_t value;
    int64_t ts_ms;
    oh_source_t source;
    bool is_input;
} oh_port_changed_t;

#define OH_MAX_ERRORS 8
#define OH_ERROR_CODE_MAX 16

esp_err_t oh_node_init(void);

/* Replace the bound module: tear down the old drivers, bind the new ones,
 * reset every port to its default, and announce OH_EVENT_MODULE_BOUND.
 * Drivers that fail to init are logged and their ports become software
 * ports; a driver id nobody registered sets the "driver" error. */
esp_err_t oh_node_bind(const oh_module_t *module);

/* Readers hold the lock while they look at the module or the states. */
void oh_node_lock(void);
void oh_node_unlock(void);
const oh_module_t *oh_node_module(void);              /* under the lock */
const oh_port_state_t *oh_node_states(void);          /* under the lock, indexed like module->ports */
void oh_node_module_copy(oh_module_t *out);           /* takes the lock itself */
size_t oh_node_snapshot(oh_port_state_t *out, size_t cap);
esp_err_t oh_node_get(uint8_t port, oh_port_desc_t *desc, oh_port_state_t *state);
bool oh_node_has_dmx(void);
bool oh_node_is_headless(void);

/* Drive an output. Validates access and type, clamps, writes the driver,
 * updates the store and posts OH_EVENT_PORT_CHANGED. */
esp_err_t oh_node_set_output(uint8_t port, const oh_value_t *value, oh_source_t source);

/* Pretend an input happened (console, tests). Goes through the same path as
 * a driver's callback. */
esp_err_t oh_node_inject_input(uint8_t port, const oh_value_t *value);

/* The health `errors` list: short machine-readable codes. */
void oh_node_error_set(const char *code);
void oh_node_error_clear(const char *code);
size_t oh_node_errors(char out[][OH_ERROR_CODE_MAX], size_t cap);
bool oh_node_has_error(const char *code);

void oh_node_identify(uint32_t duration_ms);

/* Post an OH_EVENT from anywhere (task context). */
esp_err_t oh_node_post(oh_event_id_t id, const void *data, size_t size);

#ifdef __cplusplus
}
#endif
