#include "oh_port.h"

#include <math.h>
#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

const char *oh_datatype_str(oh_datatype_t t)
{
    switch (t) {
    case OH_DT_BOOLEAN:
        return "boolean";
    case OH_DT_NUMBER:
        return "number";
    case OH_DT_STRING:
        return "string";
    case OH_DT_COLOR:
        return "color";
    }
    return "number";
}

bool oh_datatype_parse(const char *s, oh_datatype_t *out)
{
    if (!s) {
        return false;
    }
    if (strcmp(s, "boolean") == 0) {
        *out = OH_DT_BOOLEAN;
    } else if (strcmp(s, "number") == 0) {
        *out = OH_DT_NUMBER;
    } else if (strcmp(s, "string") == 0) {
        *out = OH_DT_STRING;
    } else if (strcmp(s, "color") == 0) {
        *out = OH_DT_COLOR;
    } else {
        return false;
    }
    return true;
}

const char *oh_access_str(oh_access_t a)
{
    return a == OH_ACCESS_READONLY ? "readonly" : "readwrite";
}

bool oh_access_parse(const char *s, oh_access_t *out)
{
    if (!s) {
        return false;
    }
    if (strcmp(s, "readonly") == 0) {
        *out = OH_ACCESS_READONLY;
    } else if (strcmp(s, "readwrite") == 0) {
        *out = OH_ACCESS_READWRITE;
    } else {
        return false;
    }
    return true;
}

bool oh_port_is_input(const oh_port_desc_t *p)
{
    return p->access == OH_ACCESS_READONLY;
}

void oh_value_default(const oh_port_desc_t *p, oh_value_t *out)
{
    memset(out, 0, sizeof(*out));
    out->type = p->type;
    switch (p->type) {
    case OH_DT_BOOLEAN:
        out->state = p->has_default && p->default_value != 0.0f;
        break;
    case OH_DT_NUMBER:
        out->value = p->has_default ? p->default_value : 0.0f;
        break;
    case OH_DT_STRING:
        out->text[0] = '\0';
        break;
    case OH_DT_COLOR:
        break;
    }
}

void oh_value_clamp(const oh_port_desc_t *p, oh_value_t *v)
{
    if (v->type != OH_DT_NUMBER) {
        return;
    }
    if (p->has_min && v->value < p->minimum) {
        v->value = p->minimum;
    }
    if (p->has_max && v->value > p->maximum) {
        v->value = p->maximum;
    }
}

bool oh_value_equal(const oh_value_t *a, const oh_value_t *b)
{
    if (a->type != b->type) {
        return false;
    }
    switch (a->type) {
    case OH_DT_BOOLEAN:
        return a->state == b->state;
    case OH_DT_NUMBER:
        return a->value == b->value;
    case OH_DT_STRING:
        return strcmp(a->text, b->text) == 0;
    case OH_DT_COLOR:
        return a->color.r == b->color.r && a->color.g == b->color.g && a->color.b == b->color.b;
    }
    return false;
}

oh_port_desc_t oh_port_new(uint8_t port, const char *name, oh_access_t access, oh_datatype_t type)
{
    oh_port_desc_t p;
    memset(&p, 0, sizeof(p));
    p.port = port;
    copy_str(p.name, sizeof(p.name), name);
    p.access = access;
    p.type = type;
    return p;
}

void oh_port_set_unit(oh_port_desc_t *p, const char *unit)
{
    copy_str(p->unit, sizeof(p->unit), unit);
}

void oh_port_set_class(oh_port_desc_t *p, const char *cls)
{
    copy_str(p->class_, sizeof(p->class_), cls);
}

void oh_port_set_range(oh_port_desc_t *p, float minimum, float maximum)
{
    p->minimum = minimum;
    p->maximum = maximum;
    p->has_min = 1;
    p->has_max = 1;
}

void oh_port_set_default(oh_port_desc_t *p, float value)
{
    p->default_value = value;
    p->has_default = 1;
}

void oh_port_set_effects(oh_port_desc_t *p, uint8_t effects)
{
    p->effects = effects;
}

void oh_port_set_head(oh_port_desc_t *p, uint8_t head)
{
    p->head = head > OH_HEAD_MAX ? OH_HEAD_MAX : head;
}

uint8_t oh_effect_shape_bit(int shape_index)
{
    /* Indexed by `oh_shape_t`, but without including `oh_curve.h`: this file is
     * about what a port is, and the shapes it names are a fact about the wire. */
    static const uint8_t BITS[] = {
        OH_EFFECT_SINE, OH_EFFECT_TRIANGLE, OH_EFFECT_SQUARE, OH_EFFECT_SAW_UP, OH_EFFECT_SAW_DOWN,
    };
    if (shape_index < 0 || shape_index >= (int)(sizeof(BITS) / sizeof(BITS[0]))) {
        return 0;
    }
    return BITS[shape_index];
}
