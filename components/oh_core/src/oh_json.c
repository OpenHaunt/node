#include "oh_json.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool oh_json_copy_string(const cJSON *obj, const char *key, char *out, size_t out_len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || !item->valuestring) {
        return false;
    }
    strncpy(out, item->valuestring, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

cJSON *oh_json_ok(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return o;
}

cJSON *oh_json_error(const char *message)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", false);
    cJSON_AddStringToObject(o, "error", message ? message : "error");
    return o;
}

cJSON *oh_json_port_desc(const oh_port_desc_t *p)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "port", p->port);
    cJSON_AddStringToObject(o, "name", p->name);
    cJSON_AddStringToObject(o, "access", oh_access_str(p->access));
    cJSON_AddStringToObject(o, "dataType", oh_datatype_str(p->type));
    if (p->unit[0]) {
        cJSON_AddStringToObject(o, "unit", p->unit);
    }
    if (p->has_min) {
        cJSON_AddNumberToObject(o, "minimum", p->minimum);
    }
    if (p->has_max) {
        cJSON_AddNumberToObject(o, "maximum", p->maximum);
    }
    if (p->has_default) {
        cJSON_AddNumberToObject(o, "default", p->default_value);
    }
    if (p->class_[0]) {
        cJSON_AddStringToObject(o, "class", p->class_);
    }
    return o;
}

static cJSON *dmx_json(const oh_dmx_desc_t *d)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *protocols = cJSON_AddArrayToObject(o, "protocols");
    for (int i = 0; i < d->n_protocols; i++) {
        cJSON_AddItemToArray(protocols, cJSON_CreateString(d->protocols[i]));
    }
    cJSON_AddNumberToObject(o, "universes", d->universes);
    return o;
}

cJSON *oh_json_info(const oh_module_t *m, const oh_node_identity_t *id)
{
    char type[8];
    snprintf(type, sizeof(type), "0x%04x", m->module.type);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "v", "1");
    cJSON_AddStringToObject(o, "fw", id->fw);
    cJSON_AddStringToObject(o, "sn", id->sn);
    cJSON_AddStringToObject(o, "name", id->name);

    cJSON *module = cJSON_AddObjectToObject(o, "module");
    cJSON_AddStringToObject(module, "type", type);
    cJSON_AddStringToObject(module, "name", m->module.name);
    cJSON_AddStringToObject(module, "sn", m->module.sn);
    cJSON_AddStringToObject(module, "rev", m->module.rev);
    cJSON_AddNumberToObject(module, "flags", m->module.flags);
    cJSON_AddStringToObject(module, "caps", m->module.caps);

    cJSON *ports = cJSON_AddArrayToObject(o, "ports");
    for (int i = 0; i < m->n_ports; i++) {
        cJSON_AddItemToArray(ports, oh_json_port_desc(&m->ports[i]));
    }
    /* Present only on a node that forwards a universe: its absence is what
     * tells a controller there is none to allocate. */
    if (m->has_dmx) {
        cJSON_AddItemToObject(o, "dmx", dmx_json(&m->dmx));
    }
    return o;
}

cJSON *oh_json_value(const oh_value_t *v)
{
    cJSON *o = cJSON_CreateObject();
    switch (v->type) {
    case OH_DT_BOOLEAN:
        cJSON_AddBoolToObject(o, "state", v->state);
        break;
    case OH_DT_NUMBER:
        cJSON_AddNumberToObject(o, "value", v->value);
        break;
    case OH_DT_COLOR:
        cJSON_AddNumberToObject(o, "r", v->color.r);
        cJSON_AddNumberToObject(o, "g", v->color.g);
        cJSON_AddNumberToObject(o, "b", v->color.b);
        break;
    case OH_DT_STRING:
        cJSON_AddStringToObject(o, "text", v->text);
        break;
    }
    return o;
}

cJSON *oh_json_state(const oh_module_t *m, const oh_port_state_t *states)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *outputs = cJSON_AddObjectToObject(o, "outputs");
    for (int i = 0; i < m->n_ports; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%u", m->ports[i].port);
        oh_value_t v;
        if (states && states[i].valid) {
            v = states[i].value;
        } else {
            oh_value_default(&m->ports[i], &v);
        }
        cJSON_AddItemToObject(outputs, key, oh_json_value(&v));
    }
    return o;
}

static bool byte_of(const cJSON *o, const char *key, uint8_t *out)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(n)) {
        return false;
    }
    double d = n->valuedouble;
    if (d < 0) {
        d = 0;
    }
    if (d > 255) {
        d = 255;
    }
    *out = (uint8_t)lround(d);
    return true;
}

bool oh_json_value_parse(oh_datatype_t t, const cJSON *payload, oh_value_t *out)
{
    if (!cJSON_IsObject(payload)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->type = t;
    switch (t) {
    case OH_DT_BOOLEAN: {
        const cJSON *s = cJSON_GetObjectItemCaseSensitive(payload, "state");
        if (cJSON_IsBool(s)) {
            out->state = cJSON_IsTrue(s);
        } else if (cJSON_IsNumber(s)) {
            out->state = s->valuedouble != 0;
        } else {
            return false;
        }
        return true;
    }
    case OH_DT_NUMBER: {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(payload, "value");
        if (cJSON_IsNumber(v)) {
            out->value = (float)v->valuedouble;
        } else if (cJSON_IsBool(v)) {
            out->value = cJSON_IsTrue(v) ? 1.0f : 0.0f;
        } else {
            return false;
        }
        return isfinite(out->value);
    }
    case OH_DT_COLOR:
        return byte_of(payload, "r", &out->color.r) && byte_of(payload, "g", &out->color.g) &&
               byte_of(payload, "b", &out->color.b);
    case OH_DT_STRING: {
        const cJSON *s = cJSON_GetObjectItemCaseSensitive(payload, "text");
        if (!cJSON_IsString(s) || !s->valuestring) {
            return false;
        }
        strncpy(out->text, s->valuestring, OH_TEXT_MAX - 1);
        out->text[OH_TEXT_MAX - 1] = '\0';
        return true;
    }
    }
    return false;
}

cJSON *oh_json_input_event(const oh_port_desc_t *p, const oh_value_t *v, int64_t ts_ms)
{
    cJSON *o = oh_json_value(v);
    switch (v->type) {
    case OH_DT_BOOLEAN:
        cJSON_AddStringToObject(o, "edge", v->state ? "rising" : "falling");
        break;
    case OH_DT_NUMBER:
        /* The unit the description gave, repeated so a message reads alone. */
        cJSON_AddStringToObject(o, "unit", p->unit[0] ? p->unit : "unitless");
        break;
    default:
        break;
    }
    cJSON_AddNumberToObject(o, "ts", (double)ts_ms);
    return o;
}

cJSON *oh_json_health(uint32_t uptime_s, const float *temp_c, const int *poe_class, const char *const *errors,
                      size_t n_errors)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "uptime_s", uptime_s);
    if (temp_c) {
        cJSON_AddNumberToObject(o, "temp_c", *temp_c);
    }
    if (poe_class) {
        cJSON_AddNumberToObject(o, "poe_class", *poe_class);
    }
    cJSON *errs = cJSON_AddArrayToObject(o, "errors");
    for (size_t i = 0; i < n_errors; i++) {
        cJSON_AddItemToArray(errs, cJSON_CreateString(errors[i]));
    }
    return o;
}

static bool port_key(const char *key, uint8_t *port)
{
    if (!key || !*key) {
        return false;
    }
    unsigned v = 0;
    for (const char *c = key; *c; c++) {
        if (*c < '0' || *c > '9') {
            return false;
        }
        v = v * 10 + (unsigned)(*c - '0');
        if (v > 255) {
            return false;
        }
    }
    *port = (uint8_t)v;
    return true;
}

int oh_json_apply_outputs(const cJSON *body, const oh_module_t *m, oh_json_output_fn fn, void *ctx, char *err)
{
    const cJSON *outputs = cJSON_GetObjectItemCaseSensitive(body, "outputs");
    if (!cJSON_IsObject(outputs)) {
        snprintf(err, OH_ERR_MAX, "body needs an \"outputs\" object");
        return -1;
    }
    /* Validate everything first so a body is applied whole or not at all. */
    const cJSON *entry;
    cJSON_ArrayForEach(entry, outputs) {
        uint8_t port;
        if (!port_key(entry->string, &port)) {
            snprintf(err, OH_ERR_MAX, "\"%s\" is not a port number", entry->string ? entry->string : "");
            return -1;
        }
        const oh_port_desc_t *p = oh_module_find_port(m, port);
        if (!p) {
            snprintf(err, OH_ERR_MAX, "no port %u", port);
            return -1;
        }
        if (oh_port_is_input(p)) {
            snprintf(err, OH_ERR_MAX, "port %u is readonly", port);
            return -1;
        }
        oh_value_t v;
        if (!oh_json_value_parse(p->type, entry, &v)) {
            snprintf(err, OH_ERR_MAX, "port %u takes {\"%s\": ...}", port,
                     p->type == OH_DT_BOOLEAN  ? "state"
                     : p->type == OH_DT_NUMBER ? "value"
                     : p->type == OH_DT_STRING ? "text"
                                               : "r,g,b");
            return -1;
        }
    }
    int applied = 0;
    cJSON_ArrayForEach(entry, outputs) {
        uint8_t port;
        port_key(entry->string, &port);
        const oh_port_desc_t *p = oh_module_find_port(m, port);
        oh_value_t v;
        oh_json_value_parse(p->type, entry, &v);
        oh_value_clamp(p, &v);
        fn(ctx, port, &v);
        applied++;
    }
    return applied;
}
