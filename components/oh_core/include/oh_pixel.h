/*
 * A strip output's settings and its sACN mapping (OpenHaunt/node doc 14).
 *
 * Everything here is arithmetic about a strip, kept apart from the RMT driver so the
 * host tests can hold it to the document: which universes a strip reads, where in
 * them each pixel sits, what order its bytes go out in, how fast it can be refreshed,
 * which strip gets the one DMA channel, and how a strip nobody drives goes dark.
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "oh_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OH_PIX_MAX_COUNT 1024
#define OH_PIX_MAX_FPS 40
/* Universes one strip may span: 1024 RGB pixels from address 1 is seven. */
#define OH_PIX_MAX_UNIVERSES 8
/* lwIP joins at most eight groups, and mDNS and the all-systems group take two of
 * them on each interface that is up. */
#define OH_PIX_MULTICAST_UNIVERSES 4
#define OH_PIX_ORDER_MAX 5
#define OH_PIX_HOLD_DEFAULT_MS 10000u
#define OH_PIX_FADE_MS 1000u

typedef enum {
    OH_PIX_MODE_EFFECT = 0,
    OH_PIX_MODE_SACN,
} oh_pix_mode_t;

const char *oh_pix_mode_str(oh_pix_mode_t m);
bool oh_pix_mode_parse(const char *s, oh_pix_mode_t *out);

/* ── Color order ─────────────────────────────────────────────────────── */

/* Where R, G, B (and W) go on the wire: `pos[0]` is red's byte, and so on. */
typedef struct {
    uint8_t pos[4];
    uint8_t components; /* 3 or 4 */
} oh_pix_order_t;

/* "GRB", "RGBW", …; false for anything else. */
bool oh_pix_order_parse(const char *s, oh_pix_order_t *out);
/* Every order this firmware drives, for `pixels.orders`. */
const char *const *oh_pix_orders(size_t *n);

/* ── A strip's settings ───────────────────────────────────────────────── */

typedef struct {
    oh_pix_mode_t mode;
    uint16_t count;
    char order[OH_PIX_ORDER_MAX];
    uint16_t universe; /* 0 = none set (effect mode keeps it anyway) */
    uint16_t address;  /* 1..512 */
    uint32_t hold_ms;  /* 0 = hold for ever */
} oh_pix_cfg_t;

void oh_pix_cfg_default(oh_pix_cfg_t *c);

/*
 * Merge `body` over `c` and validate the result. False with `err` (OH_ERR_MAX)
 * filled, and `c` untouched, for anything out of range — including a `sacn` strip
 * with no universe.
 */
bool oh_pix_cfg_apply_json(oh_pix_cfg_t *c, const cJSON *body, char *err);

/* The strip as `pixels.strips[]` describes it. */
cJSON *oh_pix_cfg_to_json(const oh_pix_cfg_t *c, uint8_t output, uint8_t head);

/* The whole `pixels` object for `n` strips; `heads[i]` is strip i's head. */
cJSON *oh_pix_info_json(const oh_pix_cfg_t *cfgs, const uint8_t *heads, size_t n);

/* ── The sACN mapping ─────────────────────────────────────────────────── */

typedef struct {
    uint16_t universe[OH_PIX_MAX_UNIVERSES];
    uint16_t first_pixel[OH_PIX_MAX_UNIVERSES]; /* the strip pixel each universe starts at */
    uint16_t pixels[OH_PIX_MAX_UNIVERSES];      /* how many it carries */
    uint16_t first_slot[OH_PIX_MAX_UNIVERSES];  /* 0-based slot of its first pixel */
    uint8_t n;
    uint8_t channels; /* 3 or 4 a pixel */
} oh_pix_map_t;

/* False when the strip would need more than OH_PIX_MAX_UNIVERSES, or the universe
 * range runs past 63999. */
bool oh_pix_map_build(const oh_pix_cfg_t *c, oh_pix_map_t *out);

/* Which entry of the map reads `universe`, or -1. */
int oh_pix_map_find(const oh_pix_map_t *m, uint16_t universe);

/*
 * Copy what entry `k` of the map carries out of `slots` (channel data after the
 * start code) into `rgbw`, 4 bytes a strip pixel in R G B W order. Slots the
 * frame does not reach leave their pixels as they were.
 */
void oh_pix_map_apply(const oh_pix_map_t *m, int k, const uint8_t *slots, uint16_t n_slots, uint8_t *rgbw);

/* ── Rate, memory, and going dark ─────────────────────────────────────── */

/* The most frames a second a strip of `count` WS2812s can take: 30 µs a pixel
 * and a 300 µs reset, capped at OH_PIX_MAX_FPS. */
int oh_pix_max_fps(uint16_t count);

/*
 * Which of two strips gets the S3's one DMA-capable RMT channel: the longer, strip
 * 0 on a tie, and neither when both are 64 pixels or shorter. -1 for none. A strip
 * with no pin (`count` 0) never does.
 */
int oh_pix_dma_choice(uint16_t count0, uint16_t count1);

/*
 * How much of the held frame to show, 0..1, `since_ms` after the last source of
 * the strip went away: all of it for `hold_ms`, then down to nothing over
 * OH_PIX_FADE_MS. A `hold_ms` of 0 holds for ever.
 */
float oh_pix_loss_level(uint32_t since_ms, uint32_t hold_ms);

#ifdef __cplusplus
}
#endif
