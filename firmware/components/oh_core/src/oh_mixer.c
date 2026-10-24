#include "oh_mixer.h"

#include <math.h>
#include <string.h>

#define Q32 ((uint64_t)1 << 32)

/* Per output frame, a gap in the stream fades the last sample by this much: about
 * -40 dB over 20 ms at 48 kHz, so a lost packet is a dip rather than a click. */
#define CONCEAL_DECAY 0.9952f

/* Drift: under DRIFT_IGNORE_MS nothing happens, past DRIFT_TRIM_MS the stream is
 * nudged by DRIFT_TRIM, past DRIFT_JUMP_MS it is moved outright. */
#define DRIFT_SETTLED_MS 0.25
#define DRIFT_TRIM_MS 1.0
#define DRIFT_JUMP_MS 20.0
#define DRIFT_TRIM 0.001

static uint32_t ms_frames(uint32_t ms)
{
    return (uint32_t)(((uint64_t)ms * OH_MIX_RATE) / 1000u);
}

static uint32_t ramp_frames(uint32_t ms)
{
    return ms_frames(ms < OH_MIX_MIN_RAMP_MS ? OH_MIX_MIN_RAMP_MS : ms);
}

/* The first frame of a block starting at t_us that is heard at or after `at_us`.
 * Negative when at_us is before the block. */
static int64_t frame_at(int64_t t_us, int64_t at_us)
{
    const int64_t d = at_us - t_us;
    if (d >= 0) {
        return (d * (int64_t)OH_MIX_RATE + 999999) / 1000000;
    }
    return -((-d * (int64_t)OH_MIX_RATE) / 1000000);
}

static int64_t frame_time_us(int64_t t_us, size_t i)
{
    return t_us + (int64_t)(((uint64_t)i * 1000000u) / OH_MIX_RATE);
}

static void ramp_to(oh_ramp_t *r, float target, uint32_t frames)
{
    if (frames == 0) {
        r->gain = target;
        r->target = target;
        r->left = 0;
        r->step = 0;
        return;
    }
    r->target = target;
    r->step = (target - r->gain) / (float)frames;
    r->left = frames;
}

static void ramp_tick(oh_ramp_t *r)
{
    if (r->left) {
        r->gain += r->step;
        if (--r->left == 0) {
            r->gain = r->target;
        }
    }
}

static float clamp_gain(float g)
{
    return g < 0 ? 0 : (g > 1 ? 1 : g);
}

void oh_mixer_init(oh_mixer_t *m)
{
    memset(m, 0, sizeof(*m));
    m->master.gain = 1.0f;
    m->master.target = 1.0f;
}

static void voice_finish(oh_mix_voice_t *v)
{
    if (v->active && v->src.release) {
        v->src.release(v->src.ctx);
    }
    memset(v, 0, offsetof(oh_mix_voice_t, sbuf));
    v->active = false;
}

static void voice_end(oh_mix_voice_t *v, uint32_t fade_ms)
{
    if (!v->started) {
        voice_finish(v);
        return;
    }
    v->ending = true;
    v->has_stop = false;
    ramp_to(&v->ramp, 0.0f, ramp_frames(fade_ms));
}

void oh_mixer_clear(oh_mixer_t *m)
{
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        voice_finish(&m->slots[i]);
    }
    memset(&m->stream, 0, sizeof(m->stream));
}

bool oh_mixer_play(oh_mixer_t *m, const char *name, const char *clip, const oh_mix_source_t *src, int64_t start_us,
                   uint32_t offset_ms, bool loop, float gain, uint32_t fade_in_ms)
{
    int live = 0;
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        oh_mix_voice_t *v = &m->slots[i];
        if (!v->active || v->ending || !v->name[0]) {
            continue;
        }
        if (strncmp(v->name, name, OH_VOICE_NAME_MAX) == 0) {
            v->name[0] = 0;
            voice_end(v, OH_MIX_MIN_RAMP_MS);
        } else {
            live++;
        }
    }
    if (live >= OH_MIX_VOICES) {
        /* Every voice is busy: the one that started earliest makes room. */
        oh_mix_voice_t *oldest = NULL;
        for (int i = 0; i < OH_MIX_SLOTS; i++) {
            oh_mix_voice_t *v = &m->slots[i];
            if (v->active && !v->ending && v->name[0] && (!oldest || v->order < oldest->order)) {
                oldest = v;
            }
        }
        if (oldest) {
            oldest->name[0] = 0;
            voice_end(oldest, OH_MIX_MIN_RAMP_MS);
        }
    }
    oh_mix_voice_t *v = NULL;
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        if (!m->slots[i].active) {
            v = &m->slots[i];
            break;
        }
    }
    if (!v || src->rate == 0 || src->channels < 1 || src->channels > 2) {
        if (src->release) {
            src->release(src->ctx);
        }
        return false;
    }
    memset(v, 0, offsetof(oh_mix_voice_t, sbuf));
    v->active = true;
    strncpy(v->name, name, OH_VOICE_NAME_MAX - 1);
    if (clip) {
        strncpy(v->clip, clip, OH_CLIP_ID_MAX - 1);
    }
    v->src = *src;
    v->loop = loop;
    v->start_us = start_us;
    v->offset_frames = (uint32_t)(((uint64_t)offset_ms * src->rate) / 1000u);
    v->level = clamp_gain(gain);
    v->fade_in_ms = fade_in_ms;
    v->order = ++m->next_order;
    v->step = ((uint64_t)src->rate << 32) / OH_MIX_RATE;
    return true;
}

bool oh_mixer_stop(oh_mixer_t *m, const char *name, int64_t at_us, uint32_t fade_ms)
{
    const bool all = !name || !name[0];
    bool any = false;
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        oh_mix_voice_t *v = &m->slots[i];
        if (!v->active || v->ending || (!all && strncmp(v->name, name, OH_VOICE_NAME_MAX) != 0)) {
            continue;
        }
        v->has_stop = true;
        v->stop_us = at_us;
        v->stop_fade_ms = fade_ms;
        any = true;
    }
    if (m->stream.active && !m->stream.ending && (all || strcmp(name, OH_MIX_STREAM_VOICE) == 0)) {
        m->stream.has_stop = true;
        m->stream.stop_us = at_us;
        m->stream.stop_fade_ms = fade_ms;
        any = true;
    }
    return any;
}

bool oh_mixer_gain(oh_mixer_t *m, const char *name, float gain, uint32_t fade_ms)
{
    gain = clamp_gain(gain);
    if (!name || !name[0]) {
        ramp_to(&m->master, gain, ramp_frames(fade_ms));
        return true;
    }
    if (strcmp(name, OH_MIX_STREAM_VOICE) == 0) {
        if (!m->stream.active) {
            return false;
        }
        m->stream.level = gain;
        if (m->stream.started && !m->stream.ending) {
            ramp_to(&m->stream.ramp, gain, ramp_frames(fade_ms));
        }
        return true;
    }
    bool any = false;
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        oh_mix_voice_t *v = &m->slots[i];
        if (v->active && !v->ending && strncmp(v->name, name, OH_VOICE_NAME_MAX) == 0) {
            v->level = gain;
            if (v->started) {
                ramp_to(&v->ramp, gain, ramp_frames(fade_ms));
            }
            any = true;
        }
    }
    return any;
}

void oh_mixer_stream_start(oh_mixer_t *m, oh_jitter_t *jb, uint16_t port, uint32_t rate, uint32_t ts, int64_t at_us,
                           float gain)
{
    oh_mix_stream_t *s = &m->stream;
    const bool reanchor = s->active && !s->ending && s->port == port && s->jb == jb && s->rate == rate;
    if (!reanchor) {
        memset(s, 0, sizeof(*s));
        s->active = true;
        s->jb = jb;
        s->port = port;
        s->rate = rate ? rate : OH_MIX_RATE;
        s->start_us = at_us;
    }
    s->anchor_ext = oh_jitter_widen(jb, ts);
    s->anchor_us = at_us;
    s->has_stop = false;
    s->level = clamp_gain(gain);
    if (reanchor && s->started) {
        ramp_to(&s->ramp, s->level, ramp_frames(0));
    }
}

/* ── Rendering ───────────────────────────────────────────────────────── */

/* The next source frame into `out` (both channels). False at the end of a clip that
 * does not loop. */
static bool next_frame(oh_mix_voice_t *v, int16_t out[2])
{
    if (v->si >= v->sn) {
        v->si = 0;
        v->sn = (uint16_t)v->src.pull(v->src.ctx, v->sbuf, OH_MIX_SBUF);
        if (v->sn == 0 && v->loop && v->src.frames > 0 && v->src.seek(v->src.ctx, 0)) {
            v->bpos = UINT32_MAX; /* the increment below makes it 0 */
            v->sn = (uint16_t)v->src.pull(v->src.ctx, v->sbuf, OH_MIX_SBUF);
        }
        if (v->sn == 0) {
            out[0] = out[1] = 0;
            return false;
        }
    }
    const int16_t *f = v->sbuf + (size_t)v->si * v->src.channels;
    out[0] = f[0];
    out[1] = v->src.channels == 2 ? f[1] : f[0];
    v->si++;
    v->bpos++;
    return true;
}

/* Start a voice `skip` source frames late. False if there is nothing left to play. */
static bool voice_begin(oh_mix_voice_t *v, uint64_t skip)
{
    uint64_t at = (uint64_t)v->offset_frames + skip;
    if (v->src.frames == 0) {
        return false;
    }
    if (at >= v->src.frames) {
        if (!v->loop) {
            return false;
        }
        at %= v->src.frames;
    }
    if (!v->src.seek(v->src.ctx, (uint32_t)at)) {
        return false;
    }
    v->sn = v->si = 0;
    v->bpos = (uint32_t)at - 1;
    if (!next_frame(v, v->a)) {
        return false;
    }
    v->pos = v->bpos;
    v->eos = !next_frame(v, v->b);
    v->frac = 0;
    v->started = true;
    v->ramp.gain = 0;
    uint32_t fade = v->fade_in_ms;
    if (skip && fade < OH_MIX_MIN_RAMP_MS) {
        /* Joined part way through: ramp in rather than start on a cliff. */
        fade = OH_MIX_MIN_RAMP_MS;
    }
    if (fade) {
        ramp_to(&v->ramp, v->level, ms_frames(fade));
    } else {
        ramp_to(&v->ramp, v->level, 0);
    }
    return true;
}

static void voice_render(oh_mix_voice_t *v, int64_t t_us, int32_t *acc, size_t frames)
{
    size_t i = 0;
    if (!v->started) {
        if (v->has_stop && v->stop_us <= v->start_us) {
            voice_finish(v);
            return;
        }
        const int64_t first = frame_at(t_us, v->start_us);
        if (first >= (int64_t)frames) {
            return;
        }
        uint64_t skip = 0;
        if (first < 0) {
            skip = ((uint64_t)(t_us - v->start_us) * v->src.rate) / 1000000u;
        } else {
            i = (size_t)first;
        }
        if (!voice_begin(v, skip)) {
            voice_finish(v);
            return;
        }
    }
    int64_t stop_i = INT64_MAX;
    if (v->has_stop && !v->ending) {
        stop_i = frame_at(t_us, v->stop_us);
    }
    for (; i < frames; i++) {
        if ((int64_t)i >= stop_i) {
            voice_end(v, v->stop_fade_ms);
            stop_i = INT64_MAX;
        }
        const int64_t f = (int64_t)(v->frac >> 16); /* Q16 of how far from a to b */
        const int32_t l = v->a[0] + (int32_t)(((int64_t)(v->b[0] - v->a[0]) * f) >> 16);
        const int32_t r = v->a[1] + (int32_t)(((int64_t)(v->b[1] - v->a[1]) * f) >> 16);
        const float g = v->ramp.gain;
        acc[2 * i] += (int32_t)((float)l * g);
        acc[2 * i + 1] += (int32_t)((float)r * g);
        ramp_tick(&v->ramp);
        if (v->ending && v->ramp.left == 0) {
            voice_finish(v);
            return;
        }
        v->frac += v->step;
        while (v->frac >= Q32) {
            v->frac -= Q32;
            if (v->eos) {
                /* Past the last real frame. */
                voice_finish(v);
                return;
            }
            v->a[0] = v->b[0];
            v->a[1] = v->b[1];
            v->pos = v->bpos;
            v->eos = !next_frame(v, v->b);
        }
    }
}

static void stream_render(oh_mix_stream_t *s, int64_t t_us, int32_t *acc, size_t frames)
{
    if (!s->active || !s->jb) {
        return;
    }
    size_t i = 0;
    if (!s->started) {
        if (s->has_stop && s->stop_us <= s->start_us) {
            memset(s, 0, sizeof(*s));
            return;
        }
        const int64_t first = frame_at(t_us, s->start_us);
        if (first >= (int64_t)frames) {
            return;
        }
        i = first > 0 ? (size_t)first : 0;
        s->started = true;
        s->conceal = 0;
        s->ramp.gain = 0;
        ramp_to(&s->ramp, s->level, ms_frames(OH_MIX_MIN_RAMP_MS));
    }
    /* Where the mapping says this block's first frame is, against where it is. */
    const double rate = (double)s->rate;
    const double expected = (double)s->anchor_ext + (double)(frame_time_us(t_us, i) - s->anchor_us) * rate / 1e6;
    if (!s->locked) {
        s->pos = expected;
        s->locked = true;
    } else {
        const double off_ms = (s->pos - expected) * 1000.0 / rate;
        if (fabs(off_ms) > DRIFT_JUMP_MS) {
            s->pos = expected;
            s->trim = 0;
            s->resyncs++;
        } else if (off_ms > DRIFT_TRIM_MS) {
            s->trim = -DRIFT_TRIM;
        } else if (off_ms < -DRIFT_TRIM_MS) {
            s->trim = DRIFT_TRIM;
        } else if (fabs(off_ms) < DRIFT_SETTLED_MS) {
            s->trim = 0;
        }
    }
    const double step = rate / (double)OH_MIX_RATE * (1.0 + s->trim);
    int64_t stop_i = INT64_MAX;
    if (s->has_stop && !s->ending) {
        stop_i = frame_at(t_us, s->stop_us);
    }
    oh_jitter_t *jb = s->jb;
    for (; i < frames; i++) {
        if ((int64_t)i >= stop_i) {
            s->ending = true;
            s->has_stop = false;
            ramp_to(&s->ramp, 0, ramp_frames(s->stop_fade_ms));
            stop_i = INT64_MAX;
        }
        const double fl = floor(s->pos);
        const uint64_t k = (uint64_t)fl;
        const float f = (float)(s->pos - fl);
        int16_t fa[2], fb[2];
        float l, r;
        const bool va = oh_jitter_get(jb, k, fa);
        if (va) {
            if (oh_jitter_get(jb, k + 1, fb)) {
                l = fa[0] + (fb[0] - fa[0]) * f;
                r = fa[1] + (fb[1] - fa[1]) * f;
            } else {
                l = fa[0];
                r = fa[1];
            }
            s->last[0] = fa[0];
            s->last[1] = fa[1];
            s->conceal = 1.0f;
        } else {
            s->conceal *= CONCEAL_DECAY;
            l = s->last[0] * s->conceal;
            r = s->last[1] * s->conceal;
            if (jb->have && k >= jb->first && k < jb->newest) {
                s->missing++;
            }
        }
        const float g = s->ramp.gain;
        acc[2 * i] += (int32_t)(l * g);
        acc[2 * i + 1] += (int32_t)(r * g);
        ramp_tick(&s->ramp);
        s->pos += step;
        if (s->ending && s->ramp.left == 0) {
            oh_jitter_consume(jb, (uint64_t)floor(s->pos));
            memset(s, 0, sizeof(*s));
            return;
        }
    }
    oh_jitter_consume(jb, (uint64_t)floor(s->pos));
}

static int16_t saturate(float x)
{
    if (x > 32767.0f) {
        return 32767;
    }
    if (x < -32768.0f) {
        return -32768;
    }
    return (int16_t)x;
}

void oh_mixer_render(oh_mixer_t *m, int64_t t_us, int16_t *out, size_t frames)
{
    while (frames) {
        const size_t n = frames > OH_MIX_MAX_BLOCK ? OH_MIX_MAX_BLOCK : frames;
        memset(m->acc, 0, n * 2 * sizeof(int32_t));
        for (int v = 0; v < OH_MIX_SLOTS; v++) {
            if (m->slots[v].active) {
                voice_render(&m->slots[v], t_us, m->acc, n);
            }
        }
        stream_render(&m->stream, t_us, m->acc, n);
        for (size_t i = 0; i < n; i++) {
            const float g = m->master.gain;
            out[2 * i] = saturate((float)m->acc[2 * i] * g);
            out[2 * i + 1] = saturate((float)m->acc[2 * i + 1] * g);
            ramp_tick(&m->master);
        }
        m->last_block_us = t_us;
        out += n * 2;
        t_us = frame_time_us(t_us, n);
        frames -= n;
    }
}

bool oh_mixer_busy(const oh_mixer_t *m)
{
    if (m->stream.active) {
        return true;
    }
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        if (m->slots[i].active) {
            return true;
        }
    }
    return false;
}

bool oh_mixer_sounding(const oh_mixer_t *m)
{
    if (m->stream.active && m->stream.started) {
        return true;
    }
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        if (m->slots[i].active && m->slots[i].started) {
            return true;
        }
    }
    return false;
}

int64_t oh_mixer_next_start_us(const oh_mixer_t *m)
{
    int64_t next = INT64_MAX;
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        const oh_mix_voice_t *v = &m->slots[i];
        if (v->active && !v->started && v->start_us < next) {
            next = v->start_us;
        }
    }
    if (m->stream.active && !m->stream.started && m->stream.start_us < next) {
        next = m->stream.start_us;
    }
    return next;
}

bool oh_mixer_uses_clip(const oh_mixer_t *m, const char *clip)
{
    for (int i = 0; i < OH_MIX_SLOTS; i++) {
        if (m->slots[i].active && strncmp(m->slots[i].clip, clip, OH_CLIP_ID_MAX) == 0) {
            return true;
        }
    }
    return false;
}
