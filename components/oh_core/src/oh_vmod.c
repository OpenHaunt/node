#include "oh_vmod.h"

#include "oh_curve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* `0x0003` or a plain number. */
static bool parse_type(const cJSON *item, uint16_t *out)
{
    if (cJSON_IsNumber(item)) {
        if (item->valuedouble < 0 || item->valuedouble > 0xFFFF) {
            return false;
        }
        *out = (uint16_t)item->valuedouble;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        const char *s = item->valuestring;
        while (*s == ' ') {
            s++;
        }
        char *end;
        long v = strtol(s, &end, 0);
        if (end == s || *end != '\0' || v < 0 || v > 0xFFFF) {
            return false;
        }
        *out = (uint16_t)v;
        return true;
    }
    return false;
}

static bool parse_port(const cJSON *o, oh_port_desc_t *p, char *err)
{
    memset(p, 0, sizeof(*p));
    const cJSON *port = cJSON_GetObjectItemCaseSensitive(o, "port");
    if (!cJSON_IsNumber(port) || port->valuedouble < 0 || port->valuedouble > 255) {
        snprintf(err, OH_ERR_MAX, "a port needs a \"port\" number 0..255");
        return false;
    }
    p->port = (uint8_t)port->valuedouble;
    if (!oh_json_copy_string(o, "name", p->name, sizeof(p->name)) || !p->name[0]) {
        snprintf(err, OH_ERR_MAX, "port %u needs a name", p->port);
        return false;
    }
    char buf[16];
    if (!oh_json_copy_string(o, "access", buf, sizeof(buf)) || !oh_access_parse(buf, &p->access)) {
        snprintf(err, OH_ERR_MAX, "port %u has access \"%s\"; UDR has readonly and readwrite", p->port, buf);
        return false;
    }
    if (!oh_json_copy_string(o, "dataType", buf, sizeof(buf)) || !oh_datatype_parse(buf, &p->type)) {
        snprintf(err, OH_ERR_MAX, "port %u has dataType \"%s\"; the protocol has boolean, number, string and color",
                 p->port, buf);
        return false;
    }
    oh_json_copy_string(o, "unit", p->unit, sizeof(p->unit));
    oh_json_copy_string(o, "class", p->class_, sizeof(p->class_));
    const cJSON *n;
    if (cJSON_IsNumber(n = cJSON_GetObjectItemCaseSensitive(o, "minimum"))) {
        p->minimum = (float)n->valuedouble;
        p->has_min = 1;
    }
    if (cJSON_IsNumber(n = cJSON_GetObjectItemCaseSensitive(o, "maximum"))) {
        p->maximum = (float)n->valuedouble;
        p->has_max = 1;
    }
    if (cJSON_IsNumber(n = cJSON_GetObjectItemCaseSensitive(o, "default"))) {
        p->default_value = (float)n->valuedouble;
        p->has_default = 1;
    }
    /* The same block `oh_json_port_desc` writes out, read back in, so a module
     * description round-trips through the vmod file and the EEPROM unchanged.
     * Its absence leaves `effects` at zero, which is what firmware that has never
     * heard of any of this looks like. */
    const cJSON *effects = cJSON_GetObjectItemCaseSensitive(o, "effects");
    if (cJSON_IsObject(effects)) {
        const cJSON *shapes = cJSON_GetObjectItemCaseSensitive(effects, "shapes");
        const cJSON *shape = NULL;
        cJSON_ArrayForEach(shape, shapes) {
            oh_shape_t which;
            if (cJSON_IsString(shape) && oh_shape_parse(shape->valuestring, &which)) {
                p->effects |= oh_effect_shape_bit((int)which);
            }
        }
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(effects, "steps"))) {
            p->effects |= OH_EFFECT_STEPS;
        }
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(effects, "transitions"))) {
            p->effects |= OH_EFFECT_TRANSITIONS;
        }
    }
    return true;
}

static bool parse_binding(const cJSON *o, oh_module_t *m, char *err)
{
    oh_binding_t b;
    memset(&b, 0, sizeof(b));
    if (!oh_json_copy_string(o, "driver", b.driver, sizeof(b.driver)) || !b.driver[0]) {
        snprintf(err, OH_ERR_MAX, "a binding needs a \"driver\"");
        return false;
    }
    const cJSON *port = cJSON_GetObjectItemCaseSensitive(o, "port");
    const cJSON *ports = cJSON_GetObjectItemCaseSensitive(o, "ports");
    if (cJSON_IsNumber(port)) {
        b.ports[b.n_ports++] = (uint8_t)port->valuedouble;
    } else if (cJSON_IsArray(ports)) {
        const cJSON *item;
        cJSON_ArrayForEach(item, ports) {
            if (!cJSON_IsNumber(item) || b.n_ports >= OH_MAX_PORTS) {
                snprintf(err, OH_ERR_MAX, "binding %s: \"ports\" is a list of up to %d port numbers", b.driver,
                         OH_MAX_PORTS);
                return false;
            }
            b.ports[b.n_ports++] = (uint8_t)item->valuedouble;
        }
    } else if (!ports) {
        snprintf(err, OH_ERR_MAX, "binding %s needs \"port\" or \"ports\"", b.driver);
        return false;
    }
    for (int i = 0; i < b.n_ports; i++) {
        if (!oh_module_find_port(m, b.ports[i])) {
            snprintf(err, OH_ERR_MAX, "binding %s names port %u, which is not declared", b.driver, b.ports[i]);
            return false;
        }
        for (int j = 0; j < m->n_bindings; j++) {
            for (int k = 0; k < m->bindings[j].n_ports; k++) {
                if (m->bindings[j].ports[k] == b.ports[i]) {
                    snprintf(err, OH_ERR_MAX, "port %u is bound twice", b.ports[i]);
                    return false;
                }
            }
        }
    }
    char *text = cJSON_PrintUnformatted(o);
    if (!text) {
        snprintf(err, OH_ERR_MAX, "out of memory");
        return false;
    }
    bool fits = strlen(text) < OH_BINDING_PARAMS_MAX;
    if (fits) {
        strcpy(b.params_json, text);
    }
    free(text);
    if (!fits) {
        snprintf(err, OH_ERR_MAX, "binding %s is longer than %d characters", b.driver, OH_BINDING_PARAMS_MAX - 1);
        return false;
    }
    if (m->n_bindings >= OH_MAX_BINDINGS) {
        snprintf(err, OH_ERR_MAX, "more than %d bindings", OH_MAX_BINDINGS);
        return false;
    }
    m->bindings[m->n_bindings++] = b;
    return true;
}

bool oh_vmod_parse_object(const cJSON *root, oh_module_t *out, char *err)
{
    err[0] = '\0';
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(root)) {
        snprintf(err, OH_ERR_MAX, "a module is a JSON object");
        return false;
    }
    oh_json_copy_string(root, "name", out->name, sizeof(out->name));

    const cJSON *module = cJSON_GetObjectItemCaseSensitive(root, "module");
    if (!cJSON_IsObject(module)) {
        snprintf(err, OH_ERR_MAX, "\"module\" object is missing");
        return false;
    }
    if (!parse_type(cJSON_GetObjectItemCaseSensitive(module, "type"), &out->module.type)) {
        snprintf(err, OH_ERR_MAX, "module.type is a hex string like \"0x0003\"");
        return false;
    }
    if (!oh_json_copy_string(module, "name", out->module.name, sizeof(out->module.name)) || !out->module.name[0]) {
        snprintf(err, OH_ERR_MAX, "module.name is missing");
        return false;
    }
    if (!oh_json_copy_string(module, "rev", out->module.rev, sizeof(out->module.rev))) {
        strcpy(out->module.rev, "a");
    }
    oh_json_copy_string(module, "sn", out->module.sn, sizeof(out->module.sn));
    oh_json_copy_string(module, "caps", out->module.caps, sizeof(out->module.caps));
    const cJSON *flags = cJSON_GetObjectItemCaseSensitive(module, "flags");
    if (cJSON_IsNumber(flags)) {
        out->module.flags = (uint32_t)flags->valuedouble;
    }

    const cJSON *ports = cJSON_GetObjectItemCaseSensitive(root, "ports");
    if (ports && !cJSON_IsArray(ports)) {
        snprintf(err, OH_ERR_MAX, "\"ports\" is a list");
        return false;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, ports) {
        oh_port_desc_t p;
        if (!parse_port(item, &p, err)) {
            return false;
        }
        if (oh_module_find_port(out, p.port)) {
            snprintf(err, OH_ERR_MAX, "two ports are numbered %u", p.port);
            return false;
        }
        if (!oh_module_add_port(out, &p)) {
            snprintf(err, OH_ERR_MAX, "more than %d ports", OH_MAX_PORTS);
            return false;
        }
    }

    const cJSON *dmx = cJSON_GetObjectItemCaseSensitive(root, "dmx");
    if (cJSON_IsObject(dmx)) {
        out->has_dmx = true;
        const cJSON *protocols = cJSON_GetObjectItemCaseSensitive(dmx, "protocols");
        cJSON_ArrayForEach(item, protocols) {
            if (cJSON_IsString(item) && out->dmx.n_protocols < OH_DMX_PROTOCOLS_MAX) {
                strncpy(out->dmx.protocols[out->dmx.n_protocols], item->valuestring, OH_PROTOCOL_MAX - 1);
                out->dmx.n_protocols++;
            }
        }
        const cJSON *universes = cJSON_GetObjectItemCaseSensitive(dmx, "universes");
        out->dmx.universes = cJSON_IsNumber(universes) ? (uint16_t)universes->valuedouble : 1;
        if (out->dmx.n_protocols == 0) {
            snprintf(err, OH_ERR_MAX, "dmx.protocols must list at least one of sacn, artnet");
            return false;
        }
    } else if (dmx && !cJSON_IsNull(dmx)) {
        snprintf(err, OH_ERR_MAX, "\"dmx\" is an object");
        return false;
    }

    const cJSON *bindings = cJSON_GetObjectItemCaseSensitive(root, "bindings");
    if (bindings && !cJSON_IsArray(bindings)) {
        snprintf(err, OH_ERR_MAX, "\"bindings\" is a list");
        return false;
    }
    cJSON_ArrayForEach(item, bindings) {
        if (!parse_binding(item, out, err)) {
            return false;
        }
    }
    out->source = OH_MOD_SRC_VIRTUAL;
    return true;
}

bool oh_vmod_parse(const char *json, oh_module_t *out, char *err)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        const char *at = cJSON_GetErrorPtr();
        snprintf(err, OH_ERR_MAX, "not JSON%s%.24s", at ? " near: " : "", at ? at : "");
        memset(out, 0, sizeof(*out));
        return false;
    }
    bool ok = oh_vmod_parse_object(root, out, err);
    cJSON_Delete(root);
    return ok;
}

cJSON *oh_vmod_to_json(const oh_module_t *m)
{
    char type[8];
    snprintf(type, sizeof(type), "0x%04x", m->module.type);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", m->name);
    cJSON *module = cJSON_AddObjectToObject(o, "module");
    cJSON_AddStringToObject(module, "type", type);
    cJSON_AddStringToObject(module, "name", m->module.name);
    cJSON_AddStringToObject(module, "rev", m->module.rev);
    cJSON_AddNumberToObject(module, "flags", m->module.flags);
    cJSON_AddStringToObject(module, "caps", m->module.caps);
    cJSON *ports = cJSON_AddArrayToObject(o, "ports");
    for (int i = 0; i < m->n_ports; i++) {
        cJSON_AddItemToArray(ports, oh_json_port_desc(&m->ports[i]));
    }
    if (m->has_dmx) {
        cJSON *dmx = cJSON_AddObjectToObject(o, "dmx");
        cJSON *protocols = cJSON_AddArrayToObject(dmx, "protocols");
        for (int i = 0; i < m->dmx.n_protocols; i++) {
            cJSON_AddItemToArray(protocols, cJSON_CreateString(m->dmx.protocols[i]));
        }
        cJSON_AddNumberToObject(dmx, "universes", m->dmx.universes);
    }
    cJSON *bindings = cJSON_AddArrayToObject(o, "bindings");
    for (int i = 0; i < m->n_bindings; i++) {
        cJSON *b = cJSON_Parse(m->bindings[i].params_json);
        if (!b) {
            b = cJSON_CreateObject();
            cJSON_AddStringToObject(b, "driver", m->bindings[i].driver);
        }
        cJSON_AddItemToArray(bindings, b);
    }
    return o;
}

/* ── Presets ──────────────────────────────────────────────────────────── */

static const char *const PRESET_KEYS[] = {"input", "contact", "env", "relay", "led", "oled", "dmx"};

const char *const *oh_vmod_preset_keys(size_t *n)
{
    *n = sizeof(PRESET_KEYS) / sizeof(PRESET_KEYS[0]);
    return PRESET_KEYS;
}

static void begin(oh_module_t *m, uint16_t type, const char *name, const char *caps, uint32_t flags)
{
    memset(m, 0, sizeof(*m));
    m->module.type = type;
    strncpy(m->module.name, name, OH_NAME_MAX - 1);
    strncpy(m->module.rev, "a", OH_REV_MAX - 1);
    strncpy(m->module.caps, caps, OH_CAPS_MAX - 1);
    m->module.flags = flags;
    m->source = OH_MOD_SRC_VIRTUAL;
}

static void bind1(oh_module_t *m, const char *driver, uint8_t port, const char *fmt, int a)
{
    char params[OH_BINDING_PARAMS_MAX];
    char extra[96];
    snprintf(extra, sizeof(extra), fmt, a);
    snprintf(params, sizeof(params), "{\"driver\":\"%s\",\"port\":%u%s}", driver, port, extra);
    oh_module_add_binding(m, driver, &port, 1, params);
}

static void bind_i2c(oh_module_t *m, const char *driver, const uint8_t *ports, uint8_t n, int addr, int int_gpio)
{
    char params[OH_BINDING_PARAMS_MAX];
    char list[64] = "";
    for (int i = 0; i < n; i++) {
        char item[6];
        snprintf(item, sizeof(item), "%s%u", i ? "," : "", ports[i]);
        strncat(list, item, sizeof(list) - strlen(list) - 1);
    }
    if (int_gpio >= 0) {
        snprintf(params, sizeof(params), "{\"driver\":\"%s\",\"ports\":[%s],\"addr\":%d,\"int_gpio\":%d}", driver,
                 list, addr, int_gpio);
    } else {
        snprintf(params, sizeof(params), "{\"driver\":\"%s\",\"ports\":[%s],\"addr\":%d}", driver, list, addr);
    }
    oh_module_add_binding(m, driver, ports, n, params);
}

bool oh_vmod_preset(const char *key, const oh_vmod_pins_t *pins, oh_module_t *out)
{
    oh_vmod_pins_t none;
    if (!pins) {
        memset(&none, -1, sizeof(none));
        pins = &none;
    }
    if (strcmp(key, "input") == 0) {
        begin(out, 0x0002, "Digital Inputs", "", 0);
        for (uint8_t n = 0; n < 8; n++) {
            char name[OH_NAME_MAX];
            snprintf(name, sizeof(name), "Input %u", n + 1);
            oh_port_desc_t p = oh_port_new(n, name, OH_ACCESS_READONLY, OH_DT_BOOLEAN);
            oh_port_set_class(&p, "contact");
            oh_module_add_port(out, &p);
        }
        /* On a devkit the inputs are plain GPIOs; the real module is an
         * MCP23017, which the EEPROM path binds instead. */
        for (uint8_t n = 0; n < 8; n++) {
            if (pins->inputs[n] >= 0) {
                bind1(out, "gpio_in", n, ",\"gpio\":%d,\"pull\":\"up\",\"invert\":true", pins->inputs[n]);
            }
        }
        return true;
    }
    if (strcmp(key, "contact") == 0) {
        begin(out, 0x0006, "Dry Contacts", "", 0);
        for (uint8_t n = 0; n < 4; n++) {
            char name[OH_NAME_MAX];
            snprintf(name, sizeof(name), "Contact %u", n + 1);
            oh_port_desc_t p = oh_port_new(n, name, OH_ACCESS_READWRITE, OH_DT_BOOLEAN);
            oh_port_set_default(&p, 0);
            oh_port_set_class(&p, "switch");
            oh_port_set_effects(&p, OH_EFFECT_SQUARE | OH_EFFECT_STEPS);
            oh_module_add_port(out, &p);
            if (pins->outputs[n] >= 0) {
                bind1(out, "gpio_out", n, ",\"gpio\":%d", pins->outputs[n]);
            }
        }
        return true;
    }
    if (strcmp(key, "relay") == 0) {
        begin(out, 0x0004, "Mains Relay", "", OH_MODULE_FLAG_MAINS);
        oh_port_desc_t p = oh_port_new(0, "Relay", OH_ACCESS_READWRITE, OH_DT_BOOLEAN);
        oh_port_set_default(&p, 0);
        oh_port_set_class(&p, "switch");
        /* Two states: it can be chopped and it can be stepped, and there is nothing
         * in between for a sine to trace or a fade to cross. Saying so honestly is
         * what stops a console handing it one it would ignore in silence. */
        oh_port_set_effects(&p, OH_EFFECT_SQUARE | OH_EFFECT_STEPS);
        oh_module_add_port(out, &p);
        if (pins->outputs[0] >= 0) {
            bind1(out, "gpio_out", 0, ",\"gpio\":%d", pins->outputs[0]);
        }
        return true;
    }
    if (strcmp(key, "led") == 0) {
        begin(out, 0x0003, "LED Strip", "", OH_MODULE_FLAG_FAST0);
        /* The module that can do the lot: both ports take any shape, a step list
         * and a timed move. A strip is where an operator most wants a chase, and
         * it is the one driver here fast enough to be worth handing one to. */
        const uint8_t everything = OH_EFFECT_ALL_SHAPES | OH_EFFECT_STEPS | OH_EFFECT_TRANSITIONS;
        oh_port_desc_t c = oh_port_new(0, "Strip colour", OH_ACCESS_READWRITE, OH_DT_COLOR);
        oh_port_set_class(&c, "color");
        oh_port_set_effects(&c, everything);
        oh_module_add_port(out, &c);
        oh_port_desc_t b = oh_port_new(1, "Brightness", OH_ACCESS_READWRITE, OH_DT_NUMBER);
        oh_port_set_unit(&b, "percent");
        oh_port_set_range(&b, 0, 1);
        oh_port_set_default(&b, 0);
        oh_port_set_class(&b, "intensity");
        oh_port_set_effects(&b, everything);
        oh_module_add_port(out, &b);
        if (pins->strip >= 0) {
            uint8_t ports[2] = {0, 1};
            char params[OH_BINDING_PARAMS_MAX];
            snprintf(params, sizeof(params), "{\"driver\":\"ws2812\",\"ports\":[0,1],\"gpio\":%d,\"count\":8}",
                     pins->strip);
            oh_module_add_binding(out, "ws2812", ports, 2, params);
        }
        return true;
    }
    if (strcmp(key, "oled") == 0) {
        begin(out, 0x0005, "Display", "", OH_MODULE_FLAG_I2C_EXTRA);
        oh_port_desc_t p = oh_port_new(0, "Line", OH_ACCESS_READWRITE, OH_DT_STRING);
        oh_port_set_class(&p, "text");
        oh_module_add_port(out, &p);
        uint8_t ports[1] = {0};
        bind_i2c(out, "ssd1306", ports, 1, 0x3C, -1);
        return true;
    }
    if (strcmp(key, "env") == 0) {
        begin(out, 0x0007, "Environment Sensor", "", OH_MODULE_FLAG_I2C_EXTRA);
        oh_port_desc_t t = oh_port_new(0, "Temperature", OH_ACCESS_READONLY, OH_DT_NUMBER);
        oh_port_set_unit(&t, "degree-celsius");
        oh_port_set_range(&t, -40, 85);
        oh_port_set_class(&t, "temperature");
        oh_module_add_port(out, &t);
        oh_port_desc_t h = oh_port_new(1, "Humidity", OH_ACCESS_READONLY, OH_DT_NUMBER);
        oh_port_set_unit(&h, "percent");
        oh_port_set_range(&h, 0, 100);
        oh_port_set_class(&h, "humidity");
        oh_module_add_port(out, &h);
        oh_port_desc_t a = oh_port_new(2, "Air quality", OH_ACCESS_READONLY, OH_DT_NUMBER);
        oh_port_set_unit(&a, "parts-per-million");
        oh_port_set_range(&a, 0, 5000);
        oh_port_set_class(&a, "air-quality");
        oh_module_add_port(out, &a);
        uint8_t ports[2] = {0, 1};
        bind_i2c(out, "sht4x", ports, 2, 0x44, -1);
        return true;
    }
    if (strcmp(key, "dmx") == 0) {
        begin(out, 0x0001, "DMX Gateway", "dmx,rdm,sacn",
              OH_MODULE_FLAG_UART_TX | OH_MODULE_FLAG_UART_RX | OH_MODULE_FLAG_UART_DE | OH_MODULE_FLAG_ISOLATED);
        out->has_dmx = true;
        strcpy(out->dmx.protocols[0], "sacn");
        strcpy(out->dmx.protocols[1], "artnet");
        out->dmx.n_protocols = 2;
        out->dmx.universes = 1;
        if (pins->uart_tx >= 0) {
            char params[OH_BINDING_PARAMS_MAX];
            snprintf(params, sizeof(params), "{\"driver\":\"dmx_uart\",\"ports\":[],\"uart\":1,\"tx\":%d,\"rx\":%d,\"de\":%d}",
                     pins->uart_tx, pins->uart_rx, pins->uart_de);
            oh_module_add_binding(out, "dmx_uart", NULL, 0, params);
        }
        return true;
    }
    return false;
}

/* ── Built-in I/O ─────────────────────────────────────────────────────── */

bool oh_vmod_board_v0(const oh_vmod_board_io_t *io, oh_module_t *out)
{
    begin(out, OH_MODULE_TYPE_NODE_V0, "Node V0", "", OH_MODULE_FLAG_MAINS);
    strncpy(out->module.rev, io->rev ? io->rev : "", OH_REV_MAX - 1);
    out->source = OH_MOD_SRC_BOARD;

    /* Ports 0-3: the relays, which may switch mains (G2RL, 10 A). */
    for (uint8_t n = 0; n < 4; n++) {
        char name[OH_NAME_MAX];
        snprintf(name, sizeof(name), "Relay %u", n + 1);
        oh_port_desc_t p = oh_port_new(n, name, OH_ACCESS_READWRITE, OH_DT_BOOLEAN);
        oh_port_set_default(&p, 0);
        oh_port_set_class(&p, "switch");
        oh_port_set_effects(&p, OH_EFFECT_SQUARE | OH_EFFECT_STEPS);
        if (!oh_module_add_port(out, &p)) {
            return false;
        }
        if (io->relays[n] >= 0) {
            bind1(out, "gpio_out", n, ",\"gpio\":%d", io->relays[n]);
        }
    }

    /* Ports 4-5: the sensor terminals. Both revisions pull up on the board, so
     * the GPIO needs no pull of its own. */
    for (uint8_t n = 0; n < 2; n++) {
        char name[OH_NAME_MAX];
        snprintf(name, sizeof(name), "Sensor %u", n + 1);
        uint8_t port = 4 + n;
        oh_port_desc_t p = oh_port_new(port, name, OH_ACCESS_READONLY, OH_DT_BOOLEAN);
        oh_port_set_class(&p, "contact");
        if (!oh_module_add_port(out, &p)) {
            return false;
        }
        if (io->inputs[n] >= 0) {
            bind1(out, "gpio_in", port, io->input_contact_low ? ",\"gpio\":%d,\"pull\":\"none\",\"invert\":true"
                                                              : ",\"gpio\":%d,\"pull\":\"none\"",
                  io->inputs[n]);
        }
    }

    /* Ports 6-9: the strips, a colour and a brightness each. Only the first
     * carries the classes: the console reads `color` and `intensity` as one
     * parameter per fixture, so a second strip with the same classes would
     * land on the same key. The second is named instead until the console
     * keys those by port. */
    const uint8_t everything = OH_EFFECT_ALL_SHAPES | OH_EFFECT_STEPS | OH_EFFECT_TRANSITIONS;
    for (uint8_t n = 0; n < 2; n++) {
        uint8_t ports[2] = {(uint8_t)(6 + 2 * n), (uint8_t)(7 + 2 * n)};
        char name[OH_NAME_MAX];
        snprintf(name, sizeof(name), "Strip %u colour", n + 1);
        oh_port_desc_t c = oh_port_new(ports[0], name, OH_ACCESS_READWRITE, OH_DT_COLOR);
        snprintf(name, sizeof(name), "Strip %u brightness", n + 1);
        oh_port_desc_t b = oh_port_new(ports[1], name, OH_ACCESS_READWRITE, OH_DT_NUMBER);
        oh_port_set_unit(&b, "percent");
        oh_port_set_range(&b, 0, 1);
        oh_port_set_default(&b, 0);
        if (n == 0) {
            oh_port_set_class(&c, "color");
            oh_port_set_class(&b, "intensity");
        }
        oh_port_set_effects(&c, everything);
        oh_port_set_effects(&b, everything);
        if (!oh_module_add_port(out, &c) || !oh_module_add_port(out, &b)) {
            return false;
        }
        if (io->strips[n] >= 0) {
            char params[OH_BINDING_PARAMS_MAX];
            snprintf(params, sizeof(params), "{\"driver\":\"ws2812\",\"ports\":[%u,%u],\"gpio\":%d,\"count\":%d}",
                     ports[0], ports[1], io->strips[n], io->strip_count > 0 ? io->strip_count : 1);
            if (!oh_module_add_binding(out, "ws2812", ports, 2, params)) {
                return false;
            }
        }
    }
    return true;
}
