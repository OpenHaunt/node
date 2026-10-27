#include "oh_clock_sync.h"

#include <string.h>

void oh_clock_sync_init(oh_clock_sync_t *s)
{
    memset(s, 0, sizeof(*s));
}

void oh_clock_sync_feed(oh_clock_sync_t *s, int64_t console_ms, uint64_t seq, int64_t local_ms, bool retained)
{
    const int64_t sample = console_ms - local_ms;

    /* The broker restarted, or this is a retained message from before a gap.
     * Smoothing towards it would drag the estimate through a stale value. */
    if (s->have_seq && seq < s->last_seq) {
        s->offset_ms = sample;
        s->have_offset = true;
        s->seeded = retained;
        s->last_seq = seq;
        return;
    }
    s->last_seq = seq;
    s->have_seq = true;

    if (!s->have_offset) {
        s->offset_ms = sample;
        s->have_offset = true;
        s->seeded = retained;
        return;
    }
    /* A retained sample is a starting point, never a correction. */
    if (retained) {
        return;
    }
    /* And only a starting point: it may be seconds old, and slewing out of that
     * leaves show time running fast or slow for a minute. */
    if (s->seeded) {
        s->offset_ms = sample;
        s->seeded = false;
        return;
    }

    int64_t step = (int64_t)((float)(sample - s->offset_ms) * OH_CLOCK_SMOOTHING);
    if (step > OH_CLOCK_MAX_SLEW_MS) {
        step = OH_CLOCK_MAX_SLEW_MS;
    } else if (step < -OH_CLOCK_MAX_SLEW_MS) {
        step = -OH_CLOCK_MAX_SLEW_MS;
    }
    s->offset_ms += step;
}

int64_t oh_clock_sync_now(const oh_clock_sync_t *s, int64_t local_ms)
{
    return local_ms + (s->have_offset ? s->offset_ms : 0);
}

bool oh_clock_sync_locked(const oh_clock_sync_t *s)
{
    return s->have_offset;
}
