#include "oh_node.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "oh_clock.h"

static const char *TAG = "oh_node";

ESP_EVENT_DEFINE_BASE(OH_EVENT);

typedef struct {
    const oh_driver_t *driver;
    void *inst;
    cJSON *params;
    /* local index -> module port index, -1 when unbound */
    int8_t port_index[OH_MAX_PORTS];
    uint8_t n_ports;
} bound_t;

typedef struct {
    uint8_t port;
    oh_value_t value;
    int64_t ts_us;
} input_event_t;

static struct {
    SemaphoreHandle_t lock;
    QueueHandle_t inputs;
    oh_module_t module;
    oh_port_state_t states[OH_MAX_PORTS];
    /* port index -> which binding and local index drive it */
    int8_t bound_by[OH_MAX_PORTS];
    uint8_t local_index[OH_MAX_PORTS];
    bound_t bindings[OH_MAX_BINDINGS];
    uint8_t n_bindings;
    char errors[OH_MAX_ERRORS][OH_ERROR_CODE_MAX];
    size_t n_errors;
} s;

void oh_node_lock(void)
{
    xSemaphoreTakeRecursive(s.lock, portMAX_DELAY);
}

void oh_node_unlock(void)
{
    xSemaphoreGiveRecursive(s.lock);
}

esp_err_t oh_node_post(oh_event_id_t id, const void *data, size_t size)
{
    return esp_event_post(OH_EVENT, id, data, size, pdMS_TO_TICKS(100));
}

/* ── Inputs ───────────────────────────────────────────────────────────── */

static void input_cb(void *ctx, uint8_t port, const oh_value_t *value, int64_t ts_us)
{
    (void)ctx;
    input_event_t ev = {.port = port, .value = *value, .ts_us = ts_us};
    if (xQueueSend(s.inputs, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "input queue full, dropped port %u", port);
    }
}

static void node_task(void *arg)
{
    (void)arg;
    input_event_t ev;
    for (;;) {
        if (xQueueReceive(s.inputs, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        oh_port_changed_t changed = {.port = ev.port, .source = OH_SOURCE_DRIVER, .is_input = true};
        bool publish = false;
        oh_node_lock();
        int idx = oh_module_port_index(&s.module, ev.port);
        if (idx >= 0) {
            oh_port_state_t *st = &s.states[idx];
            oh_value_t v = ev.value;
            v.type = s.module.ports[idx].type;
            oh_value_clamp(&s.module.ports[idx], &v);
            st->value = v;
            st->ts_ms = oh_clock_from_us(ev.ts_us);
            st->valid = true;
            changed.value = v;
            changed.ts_ms = st->ts_ms;
            publish = true;
        }
        oh_node_unlock();
        if (publish) {
            oh_node_post(OH_EVENT_PORT_CHANGED, &changed, sizeof(changed));
        }
    }
}

esp_err_t oh_node_inject_input(uint8_t port, const oh_value_t *value)
{
    oh_node_lock();
    const oh_port_desc_t *p = oh_module_find_port(&s.module, port);
    bool ok = p && oh_port_is_input(p);
    oh_node_unlock();
    if (!ok) {
        return ESP_ERR_NOT_FOUND;
    }
    input_cb(NULL, port, value, esp_timer_get_time());
    return ESP_OK;
}

/* ── Binding ──────────────────────────────────────────────────────────── */

static void unbind_all(void)
{
    for (int i = 0; i < s.n_bindings; i++) {
        bound_t *b = &s.bindings[i];
        if (b->driver && b->driver->deinit && b->inst) {
            b->driver->deinit(b->inst);
        }
        if (b->params) {
            cJSON_Delete(b->params);
        }
        memset(b, 0, sizeof(*b));
    }
    s.n_bindings = 0;
    memset(s.bound_by, -1, sizeof(s.bound_by));
}

static void bind_one(const oh_binding_t *binding)
{
    bound_t *b = &s.bindings[s.n_bindings];
    memset(b, 0, sizeof(*b));
    memset(b->port_index, -1, sizeof(b->port_index));
    b->driver = oh_driver_find(binding->driver);
    if (!b->driver) {
        ESP_LOGW(TAG, "no driver \"%s\"; its ports stay software ports", binding->driver);
        oh_node_error_set("driver");
        return;
    }
    b->params = cJSON_Parse(binding->params_json);
    if (!b->params) {
        b->params = cJSON_CreateObject();
    }
    oh_driver_bind_t bind = {
        .binding = binding, .params = b->params, .n_ports = binding->n_ports, .input = input_cb, .input_ctx = NULL};
    for (int i = 0; i < binding->n_ports; i++) {
        int idx = oh_module_port_index(&s.module, binding->ports[i]);
        b->port_index[i] = (int8_t)idx;
        bind.ports[i] = idx >= 0 ? &s.module.ports[idx] : NULL;
    }
    b->n_ports = binding->n_ports;
    esp_err_t err = b->driver->init(&bind, &b->inst);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "driver %s failed to init: %s", binding->driver, esp_err_to_name(err));
        oh_node_error_set("driver");
        cJSON_Delete(b->params);
        b->params = NULL;
        b->driver = NULL;
        return;
    }
    for (int i = 0; i < binding->n_ports; i++) {
        if (b->port_index[i] >= 0) {
            s.bound_by[b->port_index[i]] = (int8_t)s.n_bindings;
            s.local_index[b->port_index[i]] = (uint8_t)i;
        }
    }
    s.n_bindings++;
    ESP_LOGI(TAG, "bound %s to %d port(s)", binding->driver, binding->n_ports);
}

esp_err_t oh_node_bind(const oh_module_t *module)
{
    oh_node_lock();
    unbind_all();
    oh_node_error_clear("driver");
    s.module = *module;
    for (int i = 0; i < s.module.n_ports; i++) {
        oh_value_default(&s.module.ports[i], &s.states[i].value);
        s.states[i].ts_ms = oh_clock_now_ms();
        s.states[i].valid = true;
    }
    for (int i = 0; i < s.module.n_bindings; i++) {
        bind_one(&s.module.bindings[i]);
    }
    /* Outputs start at their defaults; inputs start at what the hardware says. */
    for (int i = 0; i < s.module.n_ports; i++) {
        if (s.bound_by[i] < 0) {
            continue;
        }
        bound_t *b = &s.bindings[s.bound_by[i]];
        uint8_t local = s.local_index[i];
        if (oh_port_is_input(&s.module.ports[i])) {
            oh_value_t v;
            if (b->driver->read && b->driver->read(b->inst, local, &v) == ESP_OK) {
                v.type = s.module.ports[i].type;
                s.states[i].value = v;
            }
        } else if (b->driver->write) {
            b->driver->write(b->inst, local, &s.states[i].value);
        }
    }
    oh_node_unlock();
    ESP_LOGI(TAG, "module 0x%04x \"%s\": %d ports, %d bindings, %s", module->module.type, module->module.name,
             module->n_ports, s.n_bindings,
             module->source == OH_MOD_SRC_EEPROM    ? "from EEPROM"
             : module->source == OH_MOD_SRC_VIRTUAL ? "virtual"
                                                    : "headless");
    return oh_node_post(OH_EVENT_MODULE_BOUND, NULL, 0);
}

/* ── Reading ──────────────────────────────────────────────────────────── */

const oh_module_t *oh_node_module(void)
{
    return &s.module;
}

const oh_port_state_t *oh_node_states(void)
{
    return s.states;
}

void oh_node_module_copy(oh_module_t *out)
{
    oh_node_lock();
    *out = s.module;
    oh_node_unlock();
}

size_t oh_node_snapshot(oh_port_state_t *out, size_t cap)
{
    oh_node_lock();
    size_t n = s.module.n_ports < cap ? s.module.n_ports : cap;
    memcpy(out, s.states, n * sizeof(*out));
    oh_node_unlock();
    return n;
}

esp_err_t oh_node_get(uint8_t port, oh_port_desc_t *desc, oh_port_state_t *state)
{
    oh_node_lock();
    int idx = oh_module_port_index(&s.module, port);
    if (idx >= 0) {
        if (desc) {
            *desc = s.module.ports[idx];
        }
        if (state) {
            *state = s.states[idx];
        }
    }
    oh_node_unlock();
    return idx >= 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool oh_node_has_dmx(void)
{
    oh_node_lock();
    bool has = s.module.has_dmx;
    oh_node_unlock();
    return has;
}

bool oh_node_is_headless(void)
{
    oh_node_lock();
    bool headless = s.module.source == OH_MOD_SRC_NONE;
    oh_node_unlock();
    return headless;
}

/* ── Writing ──────────────────────────────────────────────────────────── */

esp_err_t oh_node_set_output(uint8_t port, const oh_value_t *value, oh_source_t source)
{
    oh_node_lock();
    int idx = oh_module_port_index(&s.module, port);
    if (idx < 0) {
        oh_node_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    const oh_port_desc_t *p = &s.module.ports[idx];
    if (oh_port_is_input(p)) {
        oh_node_unlock();
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (value->type != p->type) {
        oh_node_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    oh_value_t v = *value;
    oh_value_clamp(p, &v);
    esp_err_t err = ESP_OK;
    if (s.bound_by[idx] >= 0) {
        bound_t *b = &s.bindings[s.bound_by[idx]];
        if (b->driver->write) {
            err = b->driver->write(b->inst, s.local_index[idx], &v);
        }
    }
    oh_port_changed_t changed = {.port = port, .value = v, .source = source, .is_input = false};
    if (err == ESP_OK) {
        s.states[idx].value = v;
        s.states[idx].ts_ms = oh_clock_now_ms();
        s.states[idx].valid = true;
        changed.ts_ms = s.states[idx].ts_ms;
    }
    oh_node_unlock();
    if (err == ESP_OK) {
        oh_node_post(OH_EVENT_PORT_CHANGED, &changed, sizeof(changed));
    } else {
        ESP_LOGW(TAG, "write port %u: %s", port, esp_err_to_name(err));
    }
    return err;
}

/* ── Errors ───────────────────────────────────────────────────────────── */

bool oh_node_has_error(const char *code)
{
    oh_node_lock();
    bool found = false;
    for (size_t i = 0; i < s.n_errors && !found; i++) {
        found = strcmp(s.errors[i], code) == 0;
    }
    oh_node_unlock();
    return found;
}

void oh_node_error_set(const char *code)
{
    if (oh_node_has_error(code)) {
        return;
    }
    oh_node_lock();
    if (s.n_errors < OH_MAX_ERRORS) {
        strncpy(s.errors[s.n_errors], code, OH_ERROR_CODE_MAX - 1);
        s.errors[s.n_errors][OH_ERROR_CODE_MAX - 1] = '\0';
        s.n_errors++;
    }
    oh_node_unlock();
    ESP_LOGW(TAG, "error set: %s", code);
    oh_node_post(OH_EVENT_ERRORS_CHANGED, NULL, 0);
}

void oh_node_error_clear(const char *code)
{
    bool removed = false;
    oh_node_lock();
    for (size_t i = 0; i < s.n_errors; i++) {
        if (strcmp(s.errors[i], code) == 0) {
            memmove(&s.errors[i], &s.errors[i + 1], (s.n_errors - i - 1) * OH_ERROR_CODE_MAX);
            s.n_errors--;
            removed = true;
            break;
        }
    }
    oh_node_unlock();
    if (removed) {
        ESP_LOGI(TAG, "error cleared: %s", code);
        oh_node_post(OH_EVENT_ERRORS_CHANGED, NULL, 0);
    }
}

size_t oh_node_errors(char out[][OH_ERROR_CODE_MAX], size_t cap)
{
    oh_node_lock();
    size_t n = s.n_errors < cap ? s.n_errors : cap;
    memcpy(out, s.errors, n * OH_ERROR_CODE_MAX);
    oh_node_unlock();
    return n;
}

void oh_node_identify(uint32_t duration_ms)
{
    ESP_LOGI(TAG, "identify for %lu ms", (unsigned long)duration_ms);
    oh_node_post(OH_EVENT_IDENTIFY, &duration_ms, sizeof(duration_ms));
}

/* ── Init ─────────────────────────────────────────────────────────────── */

esp_err_t oh_node_init(void)
{
    s.lock = xSemaphoreCreateRecursiveMutex();
    s.inputs = xQueueCreate(64, sizeof(input_event_t));
    if (!s.lock || !s.inputs) {
        return ESP_ERR_NO_MEM;
    }
    memset(s.bound_by, -1, sizeof(s.bound_by));
    oh_module_init_headless(&s.module);
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (xTaskCreate(node_task, "oh_node", 4096, NULL, 10, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
