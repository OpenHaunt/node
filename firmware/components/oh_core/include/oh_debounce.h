/*
 * Contact debounce that keeps the timestamp of the first edge, so the
 * event a node publishes says when the thing happened, not when it settled.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool stable;
    bool pending;
    bool pending_level;
    int64_t first_edge_us;
    int64_t settle_us;
} oh_debounce_t;

void oh_debounce_init(oh_debounce_t *d, bool initial, uint32_t settle_ms);

/* Feed a raw level at time `now_us`. Returns true when a stable transition is
 * emitted; then *level is the new stable level and *ts_us the time of the
 * first edge that led to it. Call again on a timer (with the same raw level)
 * after oh_debounce_due() to finish a pending edge without another sample. */
bool oh_debounce_feed(oh_debounce_t *d, bool raw, int64_t now_us, bool *level, int64_t *ts_us);

/* When the pending edge would settle, or -1 if nothing is pending. */
int64_t oh_debounce_due(const oh_debounce_t *d);

#ifdef __cplusplus
}
#endif
