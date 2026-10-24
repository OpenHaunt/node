#include "oh_sacn_merge.h"

#include <string.h>

void oh_sacn_merge_init(oh_sacn_merge_t *m)
{
    memset(m, 0, sizeof(*m));
}

static oh_sacn_source_t *find(oh_sacn_merge_t *m, const uint8_t *cid)
{
    for (int i = 0; i < OH_SACN_MERGE_SOURCES; i++) {
        if (m->sources[i].active && memcmp(m->sources[i].cid, cid, 16) == 0) {
            return &m->sources[i];
        }
    }
    return NULL;
}

bool oh_sacn_merge_feed(oh_sacn_merge_t *m, const oh_e131_frame_t *f, int64_t now_ms)
{
    if (f->start_code != 0 || (f->options & OH_E131_OPT_PREVIEW)) {
        return false;
    }
    oh_sacn_source_t *s = find(m, f->cid);
    if (f->options & OH_E131_OPT_TERMINATED) {
        if (s) {
            s->active = false;
        }
        return false;
    }
    if (!s) {
        for (int i = 0; i < OH_SACN_MERGE_SOURCES && !s; i++) {
            if (!m->sources[i].active) {
                s = &m->sources[i];
            }
        }
        if (!s) {
            /* Full: a source that outranks the weakest one known takes its place. */
            oh_sacn_source_t *weakest = &m->sources[0];
            for (int i = 1; i < OH_SACN_MERGE_SOURCES; i++) {
                if (m->sources[i].priority < weakest->priority) {
                    weakest = &m->sources[i];
                }
            }
            if (f->priority <= weakest->priority) {
                return false;
            }
            s = weakest;
        }
        memset(s, 0, sizeof(*s));
        memcpy(s->cid, f->cid, 16);
        s->active = true;
    }
    s->priority = f->priority;
    s->last_ms = now_ms;
    s->n_slots = f->n_slots > 512 ? 512 : f->n_slots;
    memcpy(s->slots, f->slots, s->n_slots);
    return true;
}

bool oh_sacn_merge_expire(oh_sacn_merge_t *m, int64_t now_ms)
{
    bool any = false;
    for (int i = 0; i < OH_SACN_MERGE_SOURCES; i++) {
        oh_sacn_source_t *s = &m->sources[i];
        if (s->active && now_ms - s->last_ms >= OH_SACN_SOURCE_TIMEOUT_MS) {
            s->active = false;
            any = true;
        }
    }
    return any;
}

int oh_sacn_merge_sources(const oh_sacn_merge_t *m)
{
    int n = 0;
    for (int i = 0; i < OH_SACN_MERGE_SOURCES; i++) {
        n += m->sources[i].active;
    }
    return n;
}

uint16_t oh_sacn_merge_output(const oh_sacn_merge_t *m, uint8_t *out)
{
    int top = -1;
    for (int i = 0; i < OH_SACN_MERGE_SOURCES; i++) {
        if (m->sources[i].active && (int)m->sources[i].priority > top) {
            top = m->sources[i].priority;
        }
    }
    if (top < 0) {
        return 0;
    }
    uint16_t reach = 0;
    memset(out, 0, 512);
    for (int i = 0; i < OH_SACN_MERGE_SOURCES; i++) {
        const oh_sacn_source_t *s = &m->sources[i];
        if (!s->active || (int)s->priority != top) {
            continue;
        }
        for (uint16_t k = 0; k < s->n_slots; k++) {
            if (s->slots[k] > out[k]) {
                out[k] = s->slots[k];
            }
        }
        if (s->n_slots > reach) {
            reach = s->n_slots;
        }
    }
    return reach;
}
