/*
 * One shape a port traces on its own, and one timed move.
 *
 * Both are handed over once and then run without further messages, which is the
 * whole point: a three second fade was a hundred and twenty MQTT publishes and is
 * now one, and a chase never stopped producing them at all.
 *
 * Sized to sit in a static array of `OH_MAX_PORTS`, so no allocation happens on the
 * rendering path. Steps cap at `OH_EFFECT_STEPS_MAX` and carry compact values —
 * number, boolean or color, never text — which keeps the whole table a few kB.
 *
 * No ESP-IDF dependency beyond its vendored cJSON: compiled into the host tests.
 */
#pragma once

#include "cJSON.h"
#include "oh_curve.h"
#include "oh_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sixteen keyframes per port.
 *
 * A cap rather than a list because these live in a static array: `OH_MAX_PORTS`
 * ports times sixteen steps times an `oh_value_t` is a few kB of BSS and no
 * allocator on the render path. Sixteen is comfortably more than a chase an
 * operator builds by hand, and the console is told the limit by way of the
 * protocol rather than discovering it.
 */
#define OH_EFFECT_STEPS_MAX 16

typedef struct {
    float at; /* 0..1 of the cycle */
    oh_value_t value;
    oh_easing_t easing; /* the transition into the *next* step */
} oh_effect_step_t;

typedef struct {
    bool active;
    char id[16];

    /* A shape scaled between low and high, or a list of keyframes carrying their
     * own values. `n_steps` of zero means the shape is the one in use. */
    oh_shape_t shape;
    oh_effect_step_t steps[OH_EFFECT_STEPS_MAX];
    uint8_t n_steps;

    float rate_hz;
    float phase01;
    bool backward;
    float width;
    oh_value_t low;
    oh_value_t high;
    int64_t t0_ms; /* on the console's clock, not this node's */
} oh_effect_t;

typedef struct {
    bool active;
    oh_value_t from;
    oh_value_t to;
    int64_t t0_ms; /* on the console's clock; any delay already added */
    uint32_t duration_ms;
    oh_easing_t easing;
} oh_transition_t;

/*
 * Blend two values of one type.
 *
 * A boolean and a string have no midpoint, so they turn over at halfway rather
 * than at the end: a relay under a square wave should spend half its cycle closed,
 * and switching at the first instant would leave it closed for all but a moment.
 */
void oh_value_blend(const oh_value_t *low, const oh_value_t *high, float level, oh_value_t *out);

/*
 * Parse an `output/<n>/effect` body for a port of type `t`.
 *
 * False with `err` filled for anything malformed, and false with `out->active`
 * cleared for `{"clear": true}`, which is how a port is taken back. Endpoint and
 * step values go through `oh_json_value_parse`, so the payload shape a port accepts
 * for a `set` is exactly the one it accepts inside a descriptor.
 */
bool oh_effect_parse(oh_datatype_t t, const cJSON *body, oh_effect_t *out, char *err);

/* What this effect is asserting at `now_ms` on the console's clock. */
bool oh_effect_sample(const oh_effect_t *e, int64_t now_ms, oh_value_t *out);

/*
 * The timing on a `set`, if it carries any.
 *
 * False when the body has no `fade_ms`, `delay_ms`, `curve` or `t0` — which is the
 * path every node has always had, and the reason this is additive rather than a new
 * protocol. `from` is where the port is now; `to` is the destination the caller has
 * already parsed out of the same body, because the timing keys ride *beside* an
 * ordinary payload rather than wrapping one.
 */
bool oh_transition_parse(const cJSON *body, const oh_value_t *from, const oh_value_t *to, int64_t now_ms,
                         oh_transition_t *out);

/* Where the move has got to. `*done` is set once it has arrived. */
bool oh_transition_sample(const oh_transition_t *t, int64_t now_ms, oh_value_t *out, bool *done);

#ifdef __cplusplus
}
#endif
