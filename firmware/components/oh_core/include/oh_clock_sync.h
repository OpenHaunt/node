/*
 * What this node thinks the console's clock reads.
 *
 * A shape is only as good as the clock under it. The console publishes
 * `openhaunt/clock` once a second, retained, and every node on the broker times its
 * cycles against the same answer — which is the whole reason the topic is not under
 * a serial.
 *
 * The estimate is smoothed rather than taken outright because what it corrects is
 * one-way network latency, which varies from message to message. Jumping straight to
 * each sample would jog every running effect by however much it varied.
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How far each live sample moves the estimate. A fifth: quick enough to settle in
 * a few seconds, slow enough that one late message is not a visible jolt. */
#define OH_CLOCK_SMOOTHING 0.2f

/*
 * The most a single correction may move the estimate, in milliseconds.
 *
 * Without this, one wildly late message would step every running effect by its
 * whole error at once. With it, a genuine large offset still arrives, just over a
 * few seconds — and a spurious one never lands as a visible jump.
 */
#define OH_CLOCK_MAX_SLEW_MS 50

typedef struct {
    int64_t offset_ms;
    bool have_offset;
    bool seeded; /* the estimate is a retained sample's, and no live one has come yet */
    uint64_t last_seq;
    bool have_seq;
} oh_clock_sync_t;

void oh_clock_sync_init(oh_clock_sync_t *s);

/*
 * Take one sample.
 *
 * `retained` marks a message the broker replayed on subscribe: it was published at
 * an unknown time in the past, so it seeds an empty estimate and is never allowed to
 * correct a live one. The first live sample replaces such a seed outright. A `seq`
 * that goes backwards means the broker restarted, and the estimate starts again
 * rather than being smoothed through a stale number.
 */
void oh_clock_sync_feed(oh_clock_sync_t *s, int64_t console_ms, uint64_t seq, int64_t local_ms, bool retained);

/* The console's clock now, given this node's own. */
int64_t oh_clock_sync_now(const oh_clock_sync_t *s, int64_t local_ms);

/* True once a sample has arrived. Before that, this node's own clock is the answer. */
bool oh_clock_sync_locked(const oh_clock_sync_t *s);

#ifdef __cplusplus
}
#endif
