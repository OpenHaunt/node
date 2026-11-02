/*
 * What a port driver is: a named vtable that binds to some ports, reads or
 * writes them, and reports inputs through one callback. Drivers know nothing
 * about HTTP, MQTT or the display; oh_node fans their events out.
 */
#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "oh_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `ts_us` is esp_timer_get_time() at the moment the input happened — in the
 * ISR where there is one. oh_node converts it to the wire clock. */
typedef void (*oh_input_cb_t)(void *ctx, uint8_t port, const oh_value_t *value, int64_t ts_us);

typedef struct {
    const oh_binding_t *binding;
    const cJSON *params; /* the binding's JSON object, already parsed */
    /* The descriptions of the ports this instance owns, local index order. */
    const oh_port_desc_t *ports[OH_MAX_PORTS];
    uint8_t n_ports;
    oh_input_cb_t input;
    void *input_ctx;
} oh_driver_bind_t;

typedef struct {
    const char *id;
    /* Create an instance. On success *inst is whatever the driver wants. */
    esp_err_t (*init)(const oh_driver_bind_t *bind, void **inst);
    /* Current value of local port `local`. Inputs return the last seen
     * level; outputs the last written value. May be NULL. */
    esp_err_t (*read)(void *inst, uint8_t local, oh_value_t *out);
    /* Drive local output port `local`. NULL for input-only drivers. */
    esp_err_t (*write)(void *inst, uint8_t local, const oh_value_t *in);
    void (*deinit)(void *inst);
} oh_driver_t;

esp_err_t oh_driver_register(const oh_driver_t *driver);
const oh_driver_t *oh_driver_find(const char *id);
/* All registered ids, for `oh info` and the web UI. */
size_t oh_driver_list(const char **ids, size_t cap);

/* Helpers for drivers reading their params. */
int oh_driver_param_int(const cJSON *params, const char *key, int fallback);
bool oh_driver_param_bool(const cJSON *params, const char *key, bool fallback);
const char *oh_driver_param_str(const cJSON *params, const char *key, const char *fallback);

#ifdef __cplusplus
}
#endif
