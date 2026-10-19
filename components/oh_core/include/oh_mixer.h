/*
 * The node's audio mix: up to OH_MIX_VOICES clips and one RTP stream, each started
 * and stopped at a show time, resampled to OH_MIX_RATE, summed, and clipped.
 * OpenHaunt/node doc 13.
 *
 * Time is the caller's: render() is told the show time of the first frame it is
 * writing, which on the node is when that frame will actually leave for the DAC. So
 * everything here is a pure function of the commands and the clock, and host-tested.
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "oh_rtp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OH_MIX_RATE 48000u
#define OH_MIX_VOICES 4
/* Room for a voice that is fading out after it was replaced or stolen. */
#define OH_MIX_SLOTS (OH_MIX_VOICES + 4)
#define OH_VOICE_NAME_MAX 16
#define OH_CLIP_ID_MAX 65
/* Every change of gain ramps over at least this long, so nothing clicks. */
#define OH_MIX_MIN_RAMP_MS 10u
/* The name the stream answers to in stop and gain. */
#define OH_MIX_STREAM_VOICE "stream"

/*
 * Where a voice's samples come from. pull() answers up to `n` frames, interleaved by
 * the source's channel count, and fewer at the end. seek() positions at a frame.
 * release() is called once, when the voice is finished with the source.
 */
typedef struct {
    size_t (*pull)(void *ctx, int16_t *frames, size_t n);
    bool (*seek)(void *ctx, uint32_t frame);
    void (*release)(void *ctx);
    void *ctx;
    uint8_t channels;
    uint32_t rate;
    uint32_t frames; /* length of the source */
} oh_mix_source_t;

typedef struct {
    float gain;
    float target;
    float step; /* per output frame */
    uint32_t left; /* frames until the ramp lands */
} oh_ramp_t;

#define OH_MIX_SBUF 256

typedef struct {
    bool active;
    bool started;
    bool ending; /* ramping to silence; gone when the ramp lands */
    bool eos;    /* the source has nothing more */
    char name[OH_VOICE_NAME_MAX]; /* "" once replaced: still fading, but nobody's */
    char clip[OH_CLIP_ID_MAX];
    oh_mix_source_t src;
    bool loop;
    int64_t start_us;
    uint32_t offset_frames;
    float level; /* the gain asked for, which the fade-in ramps to */
    uint32_t fade_in_ms;
    bool has_stop;
    int64_t stop_us;
    uint32_t stop_fade_ms;
    uint64_t order; /* when it was started, for stealing the oldest */
    /* Resampling: a and b are source frames `pos` and `pos + 1`; frac is how far between. */
    uint64_t step; /* Q32 source frames per output frame */
    uint64_t frac;
    int16_t a[2], b[2];
    uint32_t pos;  /* source frame index of a */
    uint32_t bpos; /* ... and of b, which a loop wraps to 0 */
    int16_t sbuf[OH_MIX_SBUF * 2];
    uint16_t sn, si;
    oh_ramp_t ramp;
} oh_mix_voice_t;

typedef struct {
    bool active;
    bool started;
    bool ending;
    oh_jitter_t *jb;
    uint16_t port;
    uint32_t rate;
    uint64_t anchor_ext; /* this widened timestamp ... */
    int64_t anchor_us;   /* ... is heard at this show time */
    double pos;          /* the widened timestamp being played */
    bool locked;         /* pos follows the mapping (after the first block) */
    double trim;         /* drift correction: 0, +0.001 or -0.001 */
    int64_t start_us;
    float level;
    bool has_stop;
    int64_t stop_us;
    uint32_t stop_fade_ms;
    int16_t last[2]; /* for concealment */
    float conceal;   /* how much of `last` is still sounding through a gap */
    uint32_t missing;
    uint32_t resyncs;
    oh_ramp_t ramp;
} oh_mix_stream_t;

#define OH_MIX_MAX_BLOCK 512

typedef struct {
    oh_mix_voice_t slots[OH_MIX_SLOTS];
    int32_t acc[OH_MIX_MAX_BLOCK * 2];
    oh_mix_stream_t stream;
    oh_ramp_t master;
    uint64_t next_order;
    int64_t last_block_us;
} oh_mixer_t;

void oh_mixer_init(oh_mixer_t *m);

/*
 * Start `src` on voice `name` at show time `start_us`, `offset_ms` into it. A voice
 * already playing under that name is faded out over OH_MIX_MIN_RAMP_MS, as is the
 * oldest voice when all OH_MIX_VOICES are busy. Answers false (and releases the
 * source) only when there is no slot at all, which the spare slots make unreachable
 * in practice.
 */
bool oh_mixer_play(oh_mixer_t *m, const char *name, const char *clip, const oh_mix_source_t *src, int64_t start_us,
                   uint32_t offset_ms, bool loop, float gain, uint32_t fade_in_ms);

/* Stop `name` ("" or NULL = every voice and the stream) at `at_us`, fading over
 * `fade_ms` (at least OH_MIX_MIN_RAMP_MS). Answers whether anything matched. */
bool oh_mixer_stop(oh_mixer_t *m, const char *name, int64_t at_us, uint32_t fade_ms);

/* Gain for `name`, or the master gain for "" / NULL, ramped over `fade_ms`. */
bool oh_mixer_gain(oh_mixer_t *m, const char *name, float gain, uint32_t fade_ms);

/*
 * Start (or re-anchor) the stream: RTP timestamp `ts` is heard at `at_us`. A start on
 * the port already playing keeps what is buffered and moves only the mapping.
 */
void oh_mixer_stream_start(oh_mixer_t *m, oh_jitter_t *jb, uint16_t port, uint32_t rate, uint32_t ts, int64_t at_us,
                           float gain);

/* Write `frames` stereo frames, the first heard at show time `t_us`. */
void oh_mixer_render(oh_mixer_t *m, int64_t t_us, int16_t *out, size_t frames);

/* True while anything is playing or about to: a voice, the stream, a fade. */
bool oh_mixer_busy(const oh_mixer_t *m);

/* True while something is actually making sound (a started voice or stream). */
bool oh_mixer_sounding(const oh_mixer_t *m);

/* The earliest show time a not-yet-started voice is due, or INT64_MAX. Lets the node
 * power an amplifier up ahead of a sound. */
int64_t oh_mixer_next_start_us(const oh_mixer_t *m);

/* True if any voice (fading or not) is still using `clip`. */
bool oh_mixer_uses_clip(const oh_mixer_t *m, const char *clip);

/* Release every source and go silent. */
void oh_mixer_clear(oh_mixer_t *m);

#ifdef __cplusplus
}
#endif
