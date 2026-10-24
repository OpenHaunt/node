#include "oh_curve.h"

#include <math.h>
#include <string.h>

#define OH_TAU 6.283185307179586f

static const char *const SHAPE_NAMES[OH_SHAPE_COUNT] = {
    "sine", "triangle", "square", "saw-up", "saw-down",
};

static const char *const EASING_NAMES[OH_EASE_COUNT] = {
    "step", "linear", "ease-in", "ease-out", "ease-in-out",
};

const char *oh_shape_str(oh_shape_t s)
{
    return (s >= 0 && s < OH_SHAPE_COUNT) ? SHAPE_NAMES[s] : "sine";
}

bool oh_shape_parse(const char *s, oh_shape_t *out)
{
    if (!s) {
        return false;
    }
    for (int i = 0; i < OH_SHAPE_COUNT; i++) {
        if (strcmp(s, SHAPE_NAMES[i]) == 0) {
            *out = (oh_shape_t)i;
            return true;
        }
    }
    return false;
}

const char *oh_easing_str(oh_easing_t e)
{
    return (e >= 0 && e < OH_EASE_COUNT) ? EASING_NAMES[e] : "linear";
}

bool oh_easing_parse(const char *s, oh_easing_t *out)
{
    if (!s) {
        return false;
    }
    for (int i = 0; i < OH_EASE_COUNT; i++) {
        if (strcmp(s, EASING_NAMES[i]) == 0) {
            *out = (oh_easing_t)i;
            return true;
        }
    }
    return false;
}

static float clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    return (v > 1.0f) ? 1.0f : v;
}

float oh_curve_sample(oh_shape_t shape, float phase01, float width)
{
    const float x = phase01;
    switch (shape) {
    case OH_SHAPE_SINE:
        return 0.5f + 0.5f * sinf(OH_TAU * x);
    case OH_SHAPE_TRIANGLE:
        /* Up over the first half, down over the second. */
        return (x < 0.5f) ? (x * 2.0f) : (2.0f - x * 2.0f);
    case OH_SHAPE_SQUARE:
        /* `width` is the duty cycle: how much of the cycle is spent high. */
        return (x < clamp01(width)) ? 1.0f : 0.0f;
    case OH_SHAPE_SAW_UP:
        return x;
    case OH_SHAPE_SAW_DOWN:
        return 1.0f - x;
    default:
        return 0.0f;
    }
}

float oh_ease(oh_easing_t easing, float t)
{
    t = clamp01(t);
    switch (easing) {
    case OH_EASE_STEP:
        /* Nothing in between: hold, then arrive. */
        return (t >= 1.0f) ? 1.0f : 0.0f;
    case OH_EASE_LINEAR:
        return t;
    case OH_EASE_IN:
        return t * t;
    case OH_EASE_OUT:
        return t * (2.0f - t);
    case OH_EASE_IN_OUT:
        return (t < 0.5f) ? (2.0f * t * t) : (1.0f - 2.0f * (1.0f - t) * (1.0f - t));
    default:
        return t;
    }
}

float oh_cycle_position(float rate_hz, bool backward, float phase01, int64_t t0_ms, int64_t now_ms)
{
    if (rate_hz <= 0.0f) {
        /* A stopped master holds every effect on it where its phase puts it,
         * however long ago that was. Dividing by the period would not survive
         * a rate of zero, and there is nothing to advance anyway. */
        float held = fmodf(phase01, 1.0f);
        return (held < 0.0f) ? held + 1.0f : held;
    }

    /* Reduce in integer milliseconds first. A period is 1000/rate ms; taking the
     * elapsed time modulo it keeps the number small enough that the conversion to
     * float below loses nothing that matters, however long the node has been up. */
    const int64_t period_ms = (int64_t)(1000.0f / rate_hz + 0.5f);
    int64_t elapsed = now_ms - t0_ms;
    if (period_ms > 0) {
        elapsed %= period_ms;
        /* C's % keeps the sign of the dividend. A node asked for a position before
         * its own anchor — which clock skew makes ordinary — must wrap to the top
         * of the cycle, not to a negative one. */
        if (elapsed < 0) {
            elapsed += period_ms;
        }
    }

    float cycles = (float)elapsed / 1000.0f * rate_hz;
    if (backward) {
        cycles = -cycles;
    }
    float x = fmodf(cycles + phase01, 1.0f);
    return (x < 0.0f) ? x + 1.0f : x;
}
