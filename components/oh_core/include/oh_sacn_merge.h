/*
 * What one universe says when more than one console is sending it (doc 14).
 *
 * Up to four sources, told apart by their CID. The highest priority present wins;
 * sources at the same priority are merged highest-takes-precedence, slot by slot —
 * the console's own input merge (the-pult task 66), so a pair of consoles behaves the
 * same in both directions. A source is gone after 2.5 s of silence, or at once when
 * it says Stream_Terminated, and Preview_Data never reaches a light.
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "oh_e131.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OH_SACN_MERGE_SOURCES 4
#define OH_SACN_SOURCE_TIMEOUT_MS 2500

typedef struct {
    bool active;
    uint8_t cid[16];
    uint8_t priority;
    int64_t last_ms;
    uint16_t n_slots;
    uint8_t slots[512];
} oh_sacn_source_t;

typedef struct {
    oh_sacn_source_t sources[OH_SACN_MERGE_SOURCES];
} oh_sacn_merge_t;

void oh_sacn_merge_init(oh_sacn_merge_t *m);

/*
 * Take one frame. True when it was taken (a data frame, not preview, and either
 * from a source already known or with room for a new one). A fifth source at a
 * priority above the lowest one known replaces it; otherwise it is ignored.
 */
bool oh_sacn_merge_feed(oh_sacn_merge_t *m, const oh_e131_frame_t *f, int64_t now_ms);

/* Drop sources silent for OH_SACN_SOURCE_TIMEOUT_MS. True when any went. */
bool oh_sacn_merge_expire(oh_sacn_merge_t *m, int64_t now_ms);

int oh_sacn_merge_sources(const oh_sacn_merge_t *m);

/*
 * The universe as it stands: 512 slots into `out`, and how many any winning
 * source actually sent. Zero, with `out` untouched, when there is no source.
 */
uint16_t oh_sacn_merge_output(const oh_sacn_merge_t *m, uint8_t *out);

#ifdef __cplusplus
}
#endif
