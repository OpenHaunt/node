/*
 * A port is one terminal of a module, in the words E1.73 UDR uses and
 * OpenHaunt/node doc 06 borrows: access, dataType, unit, range, plus a
 * `class` hint and `color` as the one extra data type.
 *
 * This header has no ESP-IDF dependency: it is compiled into the host tests
 * unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How much room an error message gets. Here rather than in `oh_json.h` because
 * both the JSON layer and the effect parser fill one, and the effect parser cannot
 * include the JSON header without a cycle. */
#define OH_ERR_MAX 96

/* 32 since the V0 strips grew their effect parameters (doc 14): four relays, two
 * sensors and nine ports per strip is 24. */
#define OH_MAX_PORTS 32
/* Heads other than 0 a node may describe (doc 14). */
#define OH_MAX_HEADS 4
#define OH_HEAD_MAX 15
#define OH_NAME_MAX 32
#define OH_UNIT_MAX 24
#define OH_CLASS_MAX 16
#define OH_TEXT_MAX 64

typedef enum {
    OH_DT_BOOLEAN = 0,
    OH_DT_NUMBER,
    OH_DT_STRING,
    OH_DT_COLOR,
} oh_datatype_t;

typedef enum {
    OH_ACCESS_READONLY = 0, /* the node produces the value: a controller input */
    OH_ACCESS_READWRITE,    /* the controller drives it: an output */
} oh_access_t;

/* One terminal as `GET /api/v1/info` describes it. */
typedef struct {
    uint8_t port;
    char name[OH_NAME_MAX];
    oh_access_t access;
    oh_datatype_t type;
    char unit[OH_UNIT_MAX];    /* "" = omitted */
    char class_[OH_CLASS_MAX]; /* "" = omitted */
    float minimum;
    float maximum;
    float default_value;
    uint8_t has_min : 1;
    uint8_t has_max : 1;
    uint8_t has_default : 1;
    /*
     * What this port can do without being sent every value, as a bit set of
     * `OH_EFFECT_*` below. Zero — the default — means the console renders and
     * streams, which is what every node did before any of this existed, so
     * firmware that never sets it behaves exactly as it always has.
     *
     * Bit-packed rather than a struct because this descriptor is memcpy'd whole
     * through the vmod round trip and stored in EEPROM, and a byte is a byte.
     */
    uint8_t effects;
    /*
     * Which head of the node the port belongs to (doc 14). Zero — the default, and
     * absent on the wire — is the node itself, which is every port that was ever
     * described before heads existed. A class is unique per head, not per node.
     */
    uint8_t head;
} oh_port_desc_t;

/* Shapes this port can trace, and the two other things it may be able to do. */
#define OH_EFFECT_SINE (1u << 0)
#define OH_EFFECT_TRIANGLE (1u << 1)
#define OH_EFFECT_SQUARE (1u << 2)
#define OH_EFFECT_SAW_UP (1u << 3)
#define OH_EFFECT_SAW_DOWN (1u << 4)
#define OH_EFFECT_STEPS (1u << 5)
#define OH_EFFECT_TRANSITIONS (1u << 6)

/* Every shape, for a port that can trace the lot. */
#define OH_EFFECT_ALL_SHAPES                                                                                 \
    (OH_EFFECT_SINE | OH_EFFECT_TRIANGLE | OH_EFFECT_SQUARE | OH_EFFECT_SAW_UP | OH_EFFECT_SAW_DOWN)

typedef struct {
    uint8_t r, g, b;
} oh_color_t;

/* A value on the wire: {state} | {value} | {r,g,b} | {text}. */
typedef struct {
    oh_datatype_t type;
    union {
        bool state;
        float value;
        oh_color_t color;
        char text[OH_TEXT_MAX];
    };
} oh_value_t;

/* What the node currently holds for one port. */
typedef struct {
    oh_value_t value;
    int64_t ts_ms;
    bool valid;
} oh_port_state_t;

const char *oh_datatype_str(oh_datatype_t t);
bool oh_datatype_parse(const char *s, oh_datatype_t *out);
const char *oh_access_str(oh_access_t a);
bool oh_access_parse(const char *s, oh_access_t *out);

bool oh_port_is_input(const oh_port_desc_t *p);

/* Where a port sits before anything drives it, from its type and `default`. */
void oh_value_default(const oh_port_desc_t *p, oh_value_t *out);

/* Clamp a number into the port's declared range, if it declared one. */
void oh_value_clamp(const oh_port_desc_t *p, oh_value_t *v);

bool oh_value_equal(const oh_value_t *a, const oh_value_t *b);

/* Fluent-ish helpers for building descriptions in C. */
oh_port_desc_t oh_port_new(uint8_t port, const char *name, oh_access_t access, oh_datatype_t type);
void oh_port_set_unit(oh_port_desc_t *p, const char *unit);
void oh_port_set_class(oh_port_desc_t *p, const char *cls);
void oh_port_set_range(oh_port_desc_t *p, float minimum, float maximum);
void oh_port_set_default(oh_port_desc_t *p, float value);
void oh_port_set_effects(oh_port_desc_t *p, uint8_t effects);
void oh_port_set_head(oh_port_desc_t *p, uint8_t head);

/* The bit for one shape index, or 0 for an index this build does not know. */
uint8_t oh_effect_shape_bit(int shape_index);

#ifdef __cplusplus
}
#endif
