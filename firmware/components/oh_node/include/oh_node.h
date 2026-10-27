/*
 * The node: the module it is being, the ports' current values, the drivers
 * bound to them, and the events everything else listens to.
 */
#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "oh_driver.h"
#include "oh_effect.h"
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
    /* A path came or went, the node perhaps staying up throughout: no data. Posted
     * after NET_UP / NET_DOWN where both apply. */
    OH_EVENT_NET_CHANGED,
    OH_EVENT_MQTT_CONNECTED, /* no data */
    OH_EVENT_MQTT_DISCONNECTED,
    OH_EVENT_ERRORS_CHANGED, /* no data */
    OH_EVENT_CONFIG_CHANGED, /* uint32_t bitmask of OH_CFG_* */
    OH_EVENT_OTA_STARTED,
    OH_EVENT_OTA_FINISHED,
    /*
     * A port tracing a shape moved. No data, and rate-limited to 4 Hz.
     *
     * Deliberately not `OH_EVENT_PORT_CHANGED`: the renderer writes forty times a
     * second per port, and posting one of those per write would flood the event
     * loop and redraw the display on every frame. Anything that needs to know
     * every single value a port took has to read the store, not listen.
     */
    OH_EVENT_RENDERED,
} oh_event_id_t;

/* How often the renderer walks the ports. 25 ms is 40 Hz: finer than DMX's refresh
 * and far finer than an eye. */
#define OH_RENDER_INTERVAL_MS 25

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
 * updates the store and posts OH_EVENT_PORT_CHANGED.
 *
 * Also cancels whatever the port was tracing: a console that has decided to send a
 * value has taken the port back, and a shape still running underneath would
 * overwrite it on the very next render tick. */
esp_err_t oh_node_set_output(uint8_t port, const oh_value_t *value, oh_source_t source);

/* Drive an output over a stated time, starting at a stated moment on the console's
 * clock. Cancels any effect on the port, exactly as a plain set does. */
esp_err_t oh_node_set_transition(uint8_t port, const oh_transition_t *transition);

/* Hand a port a shape to trace on its own. `effect->active` false clears it, which
 * leaves the port wherever the shape had got to — the console follows a clear with
 * a value precisely because this node has no opinion about where that should be. */
esp_err_t oh_node_set_effect(uint8_t port, const oh_effect_t *effect);

/* A one-line account of what a port is tracing, for /status and the console. Empty
 * when it is simply holding a value. */
void oh_node_motion_summary(uint8_t port, char *out, size_t cap);

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
