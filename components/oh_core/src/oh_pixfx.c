#include "oh_pixfx.h"

#include <math.h>
#include <string.h>

static const char *const NAMES[OH_PIXFX_COUNT] = {
    "solid", "gradient", "chase", "comet", "wave", "breathe", "fire", "candle", "sparkle", "lightning",
};

const char *oh_pixfx_str(oh_pixfx_kind_t k)
{
    return (unsigned)k < OH_PIXFX_COUNT ? NAMES[k] : "solid";
}

bool oh_pixfx_parse(const char *s, oh_pixfx_kind_t *out)
{
    *out = OH_PIXFX_SOLID;
    if (!s) {
        return false;
    }
    for (int i = 0; i < OH_PIXFX_COUNT; i++) {
        if (strcmp(s, NAMES[i]) == 0) {
            *out = (oh_pixfx_kind_t)i;
            return true;
        }
    }
    return false;
}

void oh_pixfx_defaults(oh_pixfx_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->kind = OH_PIXFX_SOLID;
    p->speed = 1.0f;
    p->size = 0.25f;
    p->segments = 1;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void oh_pixfx_clamp(oh_pixfx_params_t *p)
{
    if ((unsigned)p->kind >= OH_PIXFX_COUNT) {
        p->kind = OH_PIXFX_SOLID;
    }
    p->brightness = clampf(p->brightness, 0.0f, 1.0f);
    p->speed = clampf(p->speed, 0.0f, OH_PIXFX_SPEED_MAX);
    p->size = clampf(p->size, 0.0f, 1.0f);
    if (p->segments < 1) {
        p->segments = 1;
    } else if (p->segments > OH_PIXFX_SEGMENTS_MAX) {
        p->segments = OH_PIXFX_SEGMENTS_MAX;
    }
}

/* ── Time ─────────────────────────────────────────────────────────────────── */

#define MICRO 1000000LL

/*
 * Console time × speed, in millionths of a cycle.
 *
 * Speed is taken to the milli-hertz so the product is an exact integer: a console
 * clock of 1.7e12 ms times 10 000 mHz is 1.7e16, comfortably inside an int64, and
 * two nodes computing it get the same bits.
 */
static int64_t micro_cycles(float speed, int64_t now_ms)
{
    int64_t mhz = (int64_t)lroundf(speed * 1000.0f);
    return now_ms * mhz; /* ms × mHz = µcycles */
}

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static float frac_of(int64_t units)
{
    return (float)(units - floor_div(units, MICRO) * MICRO) / (float)MICRO;
}

float oh_pixfx_phase_at(float speed, int64_t now_ms)
{
    return frac_of(micro_cycles(speed, now_ms));
}

static float wrapf(float x)
{
    x -= floorf(x);
    return x >= 1.0f ? 0.0f : x;
}

void oh_pixfx_advance(oh_pixfx_clock_t *c, float speed, int64_t now_ms)
{
    const float ideal = oh_pixfx_phase_at(speed, now_ms);
    const int64_t dt = now_ms - c->last_ms;
    /* First frame, or a clock that jumped (a resync, a console restart): take the
     * clock's answer outright rather than integrating across the jump. */
    if (!c->started || dt < 0 || dt > 1000) {
        c->phase = ideal;
    } else {
        c->phase = wrapf(c->phase + speed * (float)dt / 1000.0f);
        if (speed == c->last_speed) {
            float diff = ideal - c->phase;
            if (diff > 0.5f) {
                diff -= 1.0f;
            } else if (diff < -0.5f) {
                diff += 1.0f;
            }
            c->phase = wrapf(c->phase + 0.02f * diff);
        }
    }
    c->started = true;
    c->last_speed = speed;
    c->last_ms = now_ms;
}

/* ── Randomness that is the same on every node ────────────────────────────── */

static uint32_t mix(uint64_t x)
{
    /* splitmix64's finaliser */
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (uint32_t)x;
}

static uint32_t hash2(int64_t a, int64_t b)
{
    return mix((uint64_t)a * 0xD6E8FEB86659FD93ull ^ mix((uint64_t)b));
}

static float unit(uint32_t h)
{
    return (float)(h >> 8) / 16777216.0f; /* 0..1, 24 bits */
}

static float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

/* Value noise on an integer lattice in space (x) and time (j + tf). */
static float noise2(float x, int64_t j, float tf, uint32_t salt)
{
    const int64_t xi = (int64_t)floorf(x);
    const float xf = x - floorf(x);
    const float sx = smooth(xf), st = smooth(tf);
    const float a = unit(hash2(xi * 31 + salt, j)), b = unit(hash2((xi + 1) * 31 + salt, j));
    const float c = unit(hash2(xi * 31 + salt, j + 1)), d = unit(hash2((xi + 1) * 31 + salt, j + 1));
    const float lo = a + (b - a) * sx, hi = c + (d - c) * sx;
    return lo + (hi - lo) * st;
}

/* ── Drawing ──────────────────────────────────────────────────────────────── */

static void blend(oh_color_t lo, oh_color_t hi, float level, float k, uint8_t *out)
{
    level = clampf(level, 0.0f, 1.0f);
    const float r = (float)lo.r + ((float)hi.r - (float)lo.r) * level;
    const float g = (float)lo.g + ((float)hi.g - (float)lo.g) * level;
    const float b = (float)lo.b + ((float)hi.b - (float)lo.b) * level;
    out[0] = (uint8_t)clampf(r * k + 0.5f, 0.0f, 255.0f);
    out[1] = (uint8_t)clampf(g * k + 0.5f, 0.0f, 255.0f);
    out[2] = (uint8_t)clampf(b * k + 0.5f, 0.0f, 255.0f);
}

/* Fire's palette: black, then Colour at half heat, then Colour 2 at full. */
static void heat_colour(const oh_pixfx_params_t *p, float heat, float k, uint8_t *out)
{
    const oh_color_t black = {0, 0, 0};
    heat = clampf(heat, 0.0f, 1.0f);
    if (heat < 0.5f) {
        blend(black, p->colour, heat * 2.0f, k, out);
    } else {
        blend(p->colour, p->colour2, (heat - 0.5f) * 2.0f, k, out);
    }
}

/* Whether a lightning strike is lit `tw_ms` into window `b`. */
static bool strike_lit(int64_t b, float window_ms, float tw_ms, float probability)
{
    if (unit(hash2(b, 101)) >= probability) {
        return false;
    }
    float t = unit(hash2(b, 102)) * 0.6f * window_ms; /* when it starts */
    const int flashes = 2 + (int)(hash2(b, 103) % 2u);
    for (int f = 0; f < flashes; f++) {
        const float on = 40.0f + unit(hash2(b, 110 + f)) * 40.0f;
        if (tw_ms >= t && tw_ms < t + on) {
            return true;
        }
        t += on + 60.0f + unit(hash2(b, 120 + f)) * 60.0f;
    }
    return false;
}

void oh_pixfx_render(const oh_pixfx_params_t *in, float phase, int64_t now_ms, uint16_t count, uint8_t *rgb)
{
    oh_pixfx_params_t p = *in;
    oh_pixfx_clamp(&p);
    const float k = p.brightness;
    const float twopi = 6.28318530718f;

    /* Effects that do not vary along the strip are worked out once. */
    const int64_t units = micro_cycles(p.speed, now_ms);
    const int64_t j = floor_div(units, MICRO);
    const float tf = frac_of(units);
    float breathe = 0.5f - 0.5f * cosf(twopi * phase);
    bool lightning = false;
    if (p.kind == OH_PIXFX_LIGHTNING && p.speed > 0.0f) {
        lightning = strike_lit(j, 1000.0f / p.speed, tf * 1000.0f / p.speed, p.size);
    }

    for (uint16_t i = 0; i < count; i++) {
        const float u = ((float)i + 0.5f) * (float)p.segments / (float)count;
        const int seg = (int)u;
        float x = u - (float)seg;
        if (p.mirror) {
            x = 1.0f - fabsf(2.0f * x - 1.0f);
        }
        if (p.reverse) {
            x = 1.0f - x;
        }
        uint8_t *out = rgb + 3 * i;
        float level = 1.0f;

        switch (p.kind) {
        case OH_PIXFX_SOLID:
            level = 1.0f;
            break;
        case OH_PIXFX_GRADIENT:
            /* Colour 2 at the start, Colour at the end, scrolling. */
            level = 1.0f - wrapf(x - phase);
            break;
        case OH_PIXFX_CHASE:
            level = wrapf(x - phase) < p.size ? 1.0f : 0.0f;
            break;
        case OH_PIXFX_COMET: {
            const float d = wrapf(phase - x);
            const float tail = p.size > 0.001f ? p.size : 0.001f;
            level = d < tail ? (1.0f - d / tail) * (1.0f - d / tail) : 0.0f;
            break;
        }
        case OH_PIXFX_WAVE: {
            const float waves = 1.0f / (p.size > 0.02f ? p.size : 0.02f);
            level = 0.5f + 0.5f * sinf(twopi * (x * waves - phase));
            break;
        }
        case OH_PIXFX_BREATHE:
            level = breathe;
            break;
        case OH_PIXFX_FIRE: {
            const float cells = 1.0f / (p.size > 0.05f ? p.size : 0.05f);
            const int64_t u2 = units * 2;
            float heat = noise2(x * cells, j, tf, 7) * 0.75f +
                         noise2(x * cells * 2.0f, floor_div(u2, MICRO), frac_of(u2), 13) * 0.35f;
            heat *= 1.0f - 0.6f * x;
            heat_colour(&p, heat, k, out);
            continue;
        }
        case OH_PIXFX_CANDLE: {
            const int64_t u3 = units * 3, u7 = units * 7;
            const float n = noise2((float)seg * 3.7f, floor_div(u3, MICRO), frac_of(u3), 29) * 0.7f +
                            noise2((float)seg * 3.7f, floor_div(u7, MICRO), frac_of(u7), 31) * 0.3f;
            const oh_color_t black = {0, 0, 0};
            blend(black, p.colour, 0.55f + 0.45f * n, k, out);
            continue;
        }
        case OH_PIXFX_SPARKLE: {
            /* Each pixel has its own offset into the flash period, so they do not all
             * flash on the same frame. */
            const int64_t mine = units + (int64_t)(hash2((int64_t)i, 1) % (uint32_t)MICRO);
            const int64_t b = floor_div(mine, MICRO);
            const float f = frac_of(mine);
            const bool lit = p.speed > 0.0f && unit(hash2((int64_t)i, b)) < p.size;
            level = lit ? (1.0f - f) * (1.0f - f) : 0.0f;
            break;
        }
        case OH_PIXFX_LIGHTNING:
            level = lightning ? 1.0f : 0.0f;
            break;
        default:
            break;
        }
        blend(p.colour2, p.colour, level, k, out);
    }
}
