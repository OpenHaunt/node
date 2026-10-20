#include "oh_effect.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "oh_json.h"

static void fail(char *err, const char *message)
{
    if (err) {
        snprintf(err, OH_ERR_MAX, "%s", message);
    }
}

static float clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    return (v > 1.0f) ? 1.0f : v;
}

static uint8_t blend_byte(uint8_t a, uint8_t b, float t)
{
    const float mixed = (float)a + ((float)b - (float)a) * t;
    if (mixed <= 0.0f) {
        return 0;
    }
    if (mixed >= 255.0f) {
        return 255;
    }
    return (uint8_t)(mixed + 0.5f);
}

void oh_value_blend(const oh_value_t *low, const oh_value_t *high, float level, oh_value_t *out)
{
    const float t = clamp01(level);
    out->type = low->type;
    switch (low->type) {
    case OH_DT_NUMBER:
        out->value = low->value + (high->value - low->value) * t;
        break;
    case OH_DT_COLOR:
        out->color.r = blend_byte(low->color.r, high->color.r, t);
        out->color.g = blend_byte(low->color.g, high->color.g, t);
        out->color.b = blend_byte(low->color.b, high->color.b, t);
        break;
    case OH_DT_BOOLEAN:
        out->state = (t >= 0.5f) ? high->state : low->state;
        break;
    case OH_DT_STRING:
    default:
        memcpy(out, (t >= 0.5f) ? high : low, sizeof(*out));
        break;
    }
}

/* ── Parsing ─────────────────────────────────────────────────────────────── */

static bool parse_steps(oh_datatype_t t, const cJSON *steps, oh_effect_t *out, char *err)
{
    const int count = cJSON_GetArraySize(steps);
    if (count <= 0) {
        fail(err, "curve.steps is empty");
        return false;
    }
    if (count > OH_EFFECT_STEPS_MAX) {
        fail(err, "curve.steps is longer than this node can hold");
        return false;
    }

    for (int i = 0; i < count; i++) {
        const cJSON *step = cJSON_GetArrayItem(steps, i);
        const cJSON *at = cJSON_GetObjectItemCaseSensitive(step, "at");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(step, "value");
        if (!cJSON_IsNumber(at) || !value) {
            fail(err, "a step needs an `at` and a `value`");
            return false;
        }
        oh_effect_step_t *slot = &out->steps[i];
        slot->at = clamp01((float)at->valuedouble);
        /* The same parser a plain `set` uses, so a step's value is accepted on
         * exactly the terms the port already accepts one. */
        if (!oh_json_value_parse(t, value, &slot->value)) {
            fail(err, "a step's value does not fit this port");
            return false;
        }
        slot->easing = OH_EASE_LINEAR;
        const cJSON *easing = cJSON_GetObjectItemCaseSensitive(step, "easing");
        if (cJSON_IsString(easing) && !oh_easing_parse(easing->valuestring, &slot->easing)) {
            fail(err, "unknown step easing");
            return false;
        }
    }
    out->n_steps = (uint8_t)count;
    return true;
}

bool oh_effect_parse(oh_datatype_t t, const cJSON *body, oh_effect_t *out, char *err)
{
    memset(out, 0, sizeof(*out));

    const cJSON *clear = cJSON_GetObjectItemCaseSensitive(body, "clear");
    if (cJSON_IsTrue(clear)) {
        /* Not an error: this is how a console takes a port back. */
        return true;
    }

    const cJSON *curve = cJSON_GetObjectItemCaseSensitive(body, "curve");
    if (!cJSON_IsObject(curve)) {
        fail(err, "an effect needs a curve");
        return false;
    }

    const cJSON *shape = cJSON_GetObjectItemCaseSensitive(curve, "shape");
    const cJSON *steps = cJSON_GetObjectItemCaseSensitive(curve, "steps");
    if (cJSON_IsString(shape)) {
        if (!oh_shape_parse(shape->valuestring, &out->shape)) {
            fail(err, "unknown shape");
            return false;
        }
    } else if (cJSON_IsArray(steps)) {
        if (!parse_steps(t, steps, out, err)) {
            return false;
        }
    } else {
        fail(err, "a curve needs a shape or a list of steps");
        return false;
    }

    const cJSON *rate = cJSON_GetObjectItemCaseSensitive(body, "rate");
    if (!cJSON_IsNumber(rate) || rate->valuedouble < 0.0) {
        fail(err, "an effect needs a rate in hertz");
        return false;
    }
    out->rate_hz = (float)rate->valuedouble;

    const cJSON *phase = cJSON_GetObjectItemCaseSensitive(body, "phase");
    out->phase01 = cJSON_IsNumber(phase) ? clamp01((float)phase->valuedouble) : 0.0f;

    const cJSON *width = cJSON_GetObjectItemCaseSensitive(body, "width");
    out->width = cJSON_IsNumber(width) ? clamp01((float)width->valuedouble) : 0.5f;

    const cJSON *direction = cJSON_GetObjectItemCaseSensitive(body, "direction");
    out->backward = cJSON_IsString(direction) && strcmp(direction->valuestring, "backward") == 0;

    const cJSON *t0 = cJSON_GetObjectItemCaseSensitive(body, "t0");
    out->t0_ms = cJSON_IsNumber(t0) ? (int64_t)t0->valuedouble : 0;

    /* Endpoints matter only to a shape; a step list carries its own values. A
     * shape with no endpoints is still renderable, between this port's own zero
     * and one, which is what a console that omitted them plainly meant. */
    const cJSON *low = cJSON_GetObjectItemCaseSensitive(body, "low");
    const cJSON *high = cJSON_GetObjectItemCaseSensitive(body, "high");
    if (out->n_steps == 0) {
        if (low && !oh_json_value_parse(t, low, &out->low)) {
            fail(err, "`low` does not fit this port");
            return false;
        }
        if (high && !oh_json_value_parse(t, high, &out->high)) {
            fail(err, "`high` does not fit this port");
            return false;
        }
        if (!low) {
            out->low.type = t;
            memset(&out->low.text, 0, sizeof(out->low.text));
        }
        if (!high) {
            out->high.type = t;
            switch (t) {
            case OH_DT_NUMBER:
                out->high.value = 1.0f;
                break;
            case OH_DT_BOOLEAN:
                out->high.state = true;
                break;
            case OH_DT_COLOR:
                out->high.color.r = out->high.color.g = out->high.color.b = 255;
                break;
            default:
                break;
            }
        }
    }

    const cJSON *id = cJSON_GetObjectItemCaseSensitive(body, "id");
    if (cJSON_IsString(id)) {
        snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
    }

    out->active = true;
    return true;
}

/* ── Rendering ───────────────────────────────────────────────────────────── */

static bool sample_steps(const oh_effect_t *e, float x, oh_value_t *out)
{
    if (e->n_steps == 0) {
        return false;
    }

    /* The step whose `at` the position has most recently passed. Steps are read in
     * the order they arrived; the console sorts them, and a list that is not sorted
     * still renders, just not as its author drew it. */
    int current = e->n_steps - 1;
    for (int i = 0; i < e->n_steps; i++) {
        if (x >= e->steps[i].at) {
            current = i;
        }
    }
    const oh_effect_step_t *step = &e->steps[current];
    const oh_effect_step_t *next = &e->steps[(current + 1) % e->n_steps];

    if (step->easing == OH_EASE_STEP || e->n_steps == 1) {
        *out = step->value;
        return true;
    }

    /* The span to the next step, wrapping past the end of the cycle. */
    float span = next->at - step->at;
    if (span <= 0.0f) {
        span += 1.0f;
    }
    float traveled = x - step->at;
    if (traveled < 0.0f) {
        traveled += 1.0f;
    }
    oh_value_blend(&step->value, &next->value, oh_ease(step->easing, traveled / span), out);
    return true;
}

bool oh_effect_sample(const oh_effect_t *e, int64_t now_ms, oh_value_t *out)
{
    if (!e->active) {
        return false;
    }
    const float x = oh_cycle_position(e->rate_hz, e->backward, e->phase01, e->t0_ms, now_ms);

    if (e->n_steps > 0) {
        return sample_steps(e, x, out);
    }
    oh_value_blend(&e->low, &e->high, oh_curve_sample(e->shape, x, e->width), out);
    return true;
}

/* ── Transitions ─────────────────────────────────────────────────────────── */

bool oh_transition_parse(const cJSON *body, const oh_value_t *from, const oh_value_t *to, int64_t now_ms,
                         oh_transition_t *out)
{
    const cJSON *fade = cJSON_GetObjectItemCaseSensitive(body, "fade_ms");
    const cJSON *delay = cJSON_GetObjectItemCaseSensitive(body, "delay_ms");
    const cJSON *curve = cJSON_GetObjectItemCaseSensitive(body, "curve");
    const cJSON *t0 = cJSON_GetObjectItemCaseSensitive(body, "t0");

    /* None of the four: the fast path, unchanged. Apply it and be done. */
    if (!fade && !delay && !curve && !t0) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->from = *from;
    out->to = *to;
    out->duration_ms = cJSON_IsNumber(fade) ? (uint32_t)fade->valuedouble : 0;
    out->t0_ms = (cJSON_IsNumber(t0) ? (int64_t)t0->valuedouble : now_ms) +
                 (cJSON_IsNumber(delay) ? (int64_t)delay->valuedouble : 0);
    out->easing = OH_EASE_LINEAR;
    if (cJSON_IsString(curve)) {
        oh_easing_parse(curve->valuestring, &out->easing);
    }
    out->active = true;
    return true;
}

bool oh_transition_sample(const oh_transition_t *t, int64_t now_ms, oh_value_t *out, bool *done)
{
    if (!t->active) {
        return false;
    }
    if (now_ms <= t->t0_ms) {
        /* Still inside its delay: hold where it started. */
        *out = t->from;
        *done = false;
        return true;
    }
    if (t->duration_ms == 0) {
        *out = t->to;
        *done = true;
        return true;
    }
    const float progress = (float)(now_ms - t->t0_ms) / (float)t->duration_ms;
    oh_value_blend(&t->from, &t->to, oh_ease(t->easing, progress), out);
    *done = progress >= 1.0f;
    return true;
}
