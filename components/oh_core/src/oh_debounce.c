#include "oh_debounce.h"

void oh_debounce_init(oh_debounce_t *d, bool initial, uint32_t settle_ms)
{
    d->stable = initial;
    d->pending = false;
    d->pending_level = initial;
    d->first_edge_us = 0;
    d->settle_us = (int64_t)settle_ms * 1000;
}

bool oh_debounce_feed(oh_debounce_t *d, bool raw, int64_t now_us, bool *level, int64_t *ts_us)
{
    if (raw == d->stable) {
        /* Back where we were: whatever bounced did not amount to an edge. */
        d->pending = false;
        return false;
    }
    if (!d->pending) {
        d->pending = true;
        d->pending_level = raw;
        d->first_edge_us = now_us;
        if (d->settle_us > 0) {
            return false;
        }
    }
    if (now_us - d->first_edge_us >= d->settle_us) {
        d->stable = raw;
        d->pending = false;
        if (level) {
            *level = raw;
        }
        if (ts_us) {
            *ts_us = d->first_edge_us;
        }
        return true;
    }
    return false;
}

int64_t oh_debounce_due(const oh_debounce_t *d)
{
    return d->pending ? d->first_edge_us + d->settle_us : -1;
}
