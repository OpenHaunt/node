/*
 * The five shapes and the five easings, evaluated on the node.
 *
 * A node that can trace a shape for itself is handed one description and then
 * left alone. What it owes the console in return is to agree about what that
 * description means, to the millisecond, without being able to ask.
 *
 * The numbers below are the same ones `pult-backend`'s `model/effects.rs` and
 * `openhaunt-node-sim`'s `motion.rs` assert, and none of the three shares code with
 * the others: they were each written from the protocol documents. All three test
 * suites carry the same table. Changing one of them and not the others shows up on
 * the bench as a node drifting out of phase with the console driving it, which is a
 * great deal harder to find than a red test.
 *
 * Sine is `0.5 + 0.5*sinf(2*pi*x)`: a cycle starts at half and peaks a quarter in.
 *
 * Every float here is `float`, with `f` suffixes and `sinf`/`fmodf` throughout. The
 * ESP32-C6 has no floating-point unit, so a stray `double` turns a handful of
 * instructions into a soft-float library call.
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OH_SHAPE_SINE = 0,
    OH_SHAPE_TRIANGLE,
    OH_SHAPE_SQUARE,
    OH_SHAPE_SAW_UP,
    OH_SHAPE_SAW_DOWN,
    OH_SHAPE_COUNT,
} oh_shape_t;

typedef enum {
    OH_EASE_STEP = 0,
    OH_EASE_LINEAR,
    OH_EASE_IN,
    OH_EASE_OUT,
    OH_EASE_IN_OUT,
    OH_EASE_COUNT,
} oh_easing_t;

/* Wire spellings: "sine", "saw-up", "ease-in-out" and so on. */
const char *oh_shape_str(oh_shape_t s);
bool oh_shape_parse(const char *s, oh_shape_t *out);
const char *oh_easing_str(oh_easing_t e);
bool oh_easing_parse(const char *s, oh_easing_t *out);

/* A shape's level at a cycle position, 0..1. `width` is the square's duty. */
float oh_curve_sample(oh_shape_t shape, float phase01, float width);

/* The shape of a transition: 0..1 in, 0..1 out. */
float oh_ease(oh_easing_t easing, float t);

/*
 * Where in its cycle an effect is at `now_ms`, 0..1.
 *
 * The millisecond arithmetic is done in `int64_t` and reduced modulo the period
 * before anything becomes a `float`. A node that has been up for five hours is
 * eighteen million milliseconds from an anchor, and a 24-bit mantissa stops being
 * able to tell one of those milliseconds from the next after about 4.7 hours — so
 * computing the elapsed time as a float first would quietly stop the effect
 * advancing smoothly, on a node that had simply been left switched on.
 */
float oh_cycle_position(float rate_hz, bool backward, float phase01, int64_t t0_ms, int64_t now_ms);

#ifdef __cplusplus
}
#endif
