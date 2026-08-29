#include "oh_module.h"

#include <string.h>

void oh_module_init_headless(oh_module_t *m)
{
    memset(m, 0, sizeof(*m));
    m->module.type = 0x0000;
    strncpy(m->module.name, "No module", OH_NAME_MAX - 1);
    strncpy(m->module.rev, "-", OH_REV_MAX - 1);
    m->source = OH_MOD_SRC_NONE;
}

int oh_module_port_index(const oh_module_t *m, uint8_t port)
{
    for (int i = 0; i < m->n_ports; i++) {
        if (m->ports[i].port == port) {
            return i;
        }
    }
    return -1;
}

const oh_port_desc_t *oh_module_find_port(const oh_module_t *m, uint8_t port)
{
    int i = oh_module_port_index(m, port);
    return i < 0 ? NULL : &m->ports[i];
}

bool oh_module_add_port(oh_module_t *m, const oh_port_desc_t *p)
{
    if (m->n_ports >= OH_MAX_PORTS || oh_module_find_port(m, p->port)) {
        return false;
    }
    m->ports[m->n_ports++] = *p;
    return true;
}

bool oh_module_add_binding(oh_module_t *m, const char *driver, const uint8_t *ports, uint8_t n_ports,
                           const char *params_json)
{
    if (m->n_bindings >= OH_MAX_BINDINGS || n_ports > OH_MAX_PORTS) {
        return false;
    }
    oh_binding_t *b = &m->bindings[m->n_bindings];
    memset(b, 0, sizeof(*b));
    strncpy(b->driver, driver, OH_DRIVER_ID_MAX - 1);
    memcpy(b->ports, ports, n_ports);
    b->n_ports = n_ports;
    if (params_json) {
        strncpy(b->params_json, params_json, OH_BINDING_PARAMS_MAX - 1);
    }
    m->n_bindings++;
    return true;
}

bool oh_module_switches_mains(const oh_module_t *m)
{
    return (m->module.flags & OH_MODULE_FLAG_MAINS) != 0;
}

bool oh_module_dmx_accepts(const oh_module_t *m, const char *protocol)
{
    if (!m->has_dmx || !protocol) {
        return false;
    }
    for (int i = 0; i < m->dmx.n_protocols; i++) {
        if (strcmp(m->dmx.protocols[i], protocol) == 0) {
            return true;
        }
    }
    return false;
}

bool oh_module_is_empty(const oh_module_t *m)
{
    return m->n_ports == 0 && !m->has_dmx;
}
