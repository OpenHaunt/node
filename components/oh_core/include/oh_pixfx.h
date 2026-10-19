/*
 * A strip in `effect` mode (OpenHaunt/node doc 14): every pixel worked out on the
 * node from nine parameters and the console's clock.
 *
 * Deterministic on purpose. The random effects are a function of the console time
 * and the pixel and nothing else, and the phase effects lock to `time × speed`, so
 * two strips told the same thing draw the same frame without talking to each other.
 *
 * Floats only, for the reason `oh_curve.h` gives: the ESP32-C6 has no FPU. Time is
 * an int64 of milliseconds and is only turned into a fraction after the integer
 * part has been taken off, so a console clock in the trillions loses nothing.
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "oh_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OH_PIXFX_SOLID = 0,
    OH_PIXFX_GRADIENT,
    OH_PIXFX_CHASE,
    OH_PIXFX_COMET,
    OH_PIXFX_WAVE,
    OH_PIXFX_BREATHE,
    OH_PIXFX_FIRE,
    OH_PIXFX_CANDLE,
    OH_PIXFX_SPARKLE,
    OH_PIXFX_LIGHTNING,
    OH_PIXFX_COUNT,
} oh_pixfx_kind_t;

const char *oh_pixfx_str(oh_pixfx_kind_t k);
/* False for a name this build does not know; `*out` is then `OH_PIXFX_SOLID`. */
bool oh_pixfx_parse(const char *s, oh_pixfx_kind_t *out);

#define OH_PIXFX_SPEED_MAX 10.0f
#define OH_PIXFX_SEGMENTS_MAX 16

typedef struct {
    oh_pixfx_kind_t kind;
    oh_color_t colour;  /* level 1 */
    oh_color_t colour2; /* level 0 */
    float brightness;   /* 0..1 */
    float speed;        /* cycles a second, 0..10 */
    float size;         /* 0..1 of a segment */
    bool reverse;
    bool mirror;
    int segments; /* 1..16 */
} oh_pixfx_params_t;

/* What every parameter is before the console says anything: the old strip. */
void oh_pixfx_defaults(oh_pixfx_params_t *p);

/* Clamp everything into its documented range. */
void oh_pixfx_clamp(oh_pixfx_params_t *p);

/*
 * The phase of a strip, 0..1.
 *
 * Advanced by speed × elapsed time, so a fade on Speed never makes the pattern jump;
 * and, while the speed holds still, pulled 2% a frame towards `time × speed` on the
 * console clock, so two strips started apart end up in step.
 */
typedef struct {
    float phase;
    float last_speed;
    int64_t last_ms;
    bool started;
} oh_pixfx_clock_t;

/* Where `time × speed` is, 0..1, exactly: speed is taken to the milli-hertz. */
float oh_pixfx_phase_at(float speed, int64_t now_ms);

void oh_pixfx_advance(oh_pixfx_clock_t *c, float speed, int64_t now_ms);

/*
 * Draw one frame into `rgb` (3 bytes a pixel, R G B) — brightness applied, no
 * gamma. `phase` is the clock's; `now_ms` drives the random effects.
 */
void oh_pixfx_render(const oh_pixfx_params_t *p, float phase, int64_t now_ms, uint16_t count, uint8_t *rgb);

#ifdef __cplusplus
}
#endif
