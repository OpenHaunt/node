#include "oh_wav.h"

#include <stdio.h>
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool fail(char err[OH_ERR_MAX], const char *why)
{
    if (err) {
        snprintf(err, OH_ERR_MAX, "%s", why);
    }
    return false;
}

const char *oh_wav_format_str(uint16_t format)
{
    switch (format) {
    case OH_WAV_PCM16:
        return "pcm16";
    case OH_WAV_IMA_ADPCM:
        return "ima-adpcm";
    default:
        return "unknown";
    }
}

/* Frames in a trailing ADPCM block that is shorter than block_align. */
static uint32_t partial_block_frames(uint32_t bytes, uint8_t channels)
{
    const uint32_t header = 4u * channels;
    if (bytes < header) {
        return 0;
    }
    /* After the header, every 4 bytes per channel are eight samples of that channel. */
    return 1 + ((bytes - header) / (4u * channels)) * 8u;
}

bool oh_wav_parse(const uint8_t *buf, size_t len, uint32_t file_len, oh_wav_info_t *out, char err[OH_ERR_MAX])
{
    memset(out, 0, sizeof(*out));
    if (len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        return fail(err, "not a RIFF WAVE file");
    }
    bool have_fmt = false;
    uint32_t fact_frames = 0;
    size_t at = 12;
    while (at + 8 <= len) {
        const uint8_t *chunk = buf + at;
        const uint32_t size = le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (size < 16 || at + 8 + size > len) {
                return fail(err, "short fmt chunk");
            }
            const uint8_t *f = chunk + 8;
            out->format = le16(f);
            out->channels = (uint8_t)le16(f + 2);
            out->rate = le32(f + 4);
            out->block_align = le16(f + 12);
            const uint16_t bits = le16(f + 14);
            if (out->format == 0xFFFE && size >= 40) {
                /* WAVE_FORMAT_EXTENSIBLE: the real tag is the subformat GUID's first two bytes. */
                out->format = le16(f + 24);
            }
            if (out->channels < 1 || out->channels > 2) {
                return fail(err, "only mono or stereo");
            }
            if (out->rate < 4000 || out->rate > 96000) {
                return fail(err, "sample rate out of range");
            }
            if (out->format == OH_WAV_PCM16) {
                if (bits != 16 || out->block_align != 2u * out->channels) {
                    return fail(err, "PCM must be 16-bit");
                }
                out->frames_per_block = 1;
            } else if (out->format == OH_WAV_IMA_ADPCM) {
                if (bits != 4) {
                    return fail(err, "IMA ADPCM must be 4-bit");
                }
                if (out->block_align > OH_WAV_MAX_BLOCK || out->block_align < 4u * out->channels + 4u * out->channels) {
                    return fail(err, "ADPCM block size unsupported");
                }
                if ((out->block_align - 4u * out->channels) % (4u * out->channels) != 0) {
                    return fail(err, "ADPCM block size unsupported");
                }
                out->frames_per_block = (uint16_t)partial_block_frames(out->block_align, out->channels);
            } else {
                return fail(err, "only PCM16 or IMA ADPCM");
            }
            have_fmt = true;
        } else if (memcmp(chunk, "fact", 4) == 0 && size >= 4 && at + 12 <= len) {
            fact_frames = le32(chunk + 8);
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!have_fmt) {
                return fail(err, "data before fmt");
            }
            out->data_offset = (uint32_t)(at + 8);
            out->data_bytes = size;
            if (file_len && (uint64_t)out->data_offset + size > file_len) {
                /* Some writers leave the size at 0 or 0xFFFFFFFF when they stream; what
                 * is actually there is what counts. */
                if (size == 0 || size == 0xFFFFFFFFu) {
                    out->data_bytes = file_len - out->data_offset;
                } else {
                    return fail(err, "data chunk runs past the end of the file");
                }
            }
            if (out->format == OH_WAV_PCM16) {
                out->frames = out->data_bytes / out->block_align;
            } else {
                const uint32_t whole = out->data_bytes / out->block_align;
                out->frames = whole * out->frames_per_block +
                              partial_block_frames(out->data_bytes % out->block_align, out->channels);
                if (fact_frames && fact_frames < out->frames) {
                    out->frames = fact_frames;
                }
            }
            if (out->frames == 0) {
                return fail(err, "no samples");
            }
            return true;
        }
        at += 8 + size + (size & 1u); /* chunks are word-aligned */
    }
    return fail(err, have_fmt ? "no data chunk near the start" : "no fmt chunk");
}

uint32_t oh_wav_duration_ms(const oh_wav_info_t *info)
{
    return info->rate ? (uint32_t)(((uint64_t)info->frames * 1000u) / info->rate) : 0;
}

/* ── IMA ADPCM ───────────────────────────────────────────────────────── */

static const int16_t ima_step[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static const int8_t ima_index[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

typedef struct {
    int32_t pred;
    int32_t index;
} ima_state_t;

static int16_t ima_nibble(ima_state_t *s, uint8_t n)
{
    const int32_t step = ima_step[s->index];
    int32_t diff = step >> 3;
    if (n & 1) {
        diff += step >> 2;
    }
    if (n & 2) {
        diff += step >> 1;
    }
    if (n & 4) {
        diff += step;
    }
    s->pred += (n & 8) ? -diff : diff;
    if (s->pred > 32767) {
        s->pred = 32767;
    } else if (s->pred < -32768) {
        s->pred = -32768;
    }
    s->index += ima_index[n];
    if (s->index < 0) {
        s->index = 0;
    } else if (s->index > 88) {
        s->index = 88;
    }
    return (int16_t)s->pred;
}

size_t oh_ima_decode_block(const uint8_t *block, size_t len, uint8_t channels, int16_t *out, size_t max_frames)
{
    if (channels < 1 || channels > 2 || len < 4u * channels || max_frames == 0) {
        return 0;
    }
    ima_state_t st[2];
    for (uint8_t c = 0; c < channels; c++) {
        const uint8_t *h = block + 4u * c;
        st[c].pred = (int16_t)le16(h);
        st[c].index = h[2] > 88 ? 88 : h[2];
        out[c] = (int16_t)st[c].pred;
    }
    size_t frames = 1;
    const uint8_t *p = block + 4u * channels;
    const uint8_t *end = block + len;
    /* One group is 4 bytes per channel: eight samples of each. */
    while (p + 4u * channels <= end && frames < max_frames) {
        for (uint8_t c = 0; c < channels; c++) {
            const uint8_t *g = p + 4u * c;
            for (int i = 0; i < 8; i++) {
                const uint8_t byte = g[i >> 1];
                const uint8_t n = (i & 1) ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0F);
                const int16_t v = ima_nibble(&st[c], n);
                const size_t f = frames + (size_t)i;
                if (f < max_frames) {
                    out[f * channels + c] = v;
                }
            }
        }
        frames += 8;
        p += 4u * channels;
    }
    return frames < max_frames ? frames : max_frames;
}

/* ── Reader ──────────────────────────────────────────────────────────── */

void oh_wav_reader_init(oh_wav_reader_t *r, const oh_wav_info_t *info, const oh_io_t *io)
{
    r->info = *info;
    r->io = *io;
    r->frame = 0;
    r->block_frames = 0;
    r->block_pos = 0;
}

static bool load_block(oh_wav_reader_t *r, uint32_t block)
{
    const oh_wav_info_t *in = &r->info;
    const uint32_t offset = block * in->block_align;
    if (offset >= in->data_bytes) {
        r->block_frames = 0;
        return false;
    }
    uint32_t want = in->data_bytes - offset;
    if (want > in->block_align) {
        want = in->block_align;
    }
    if (!r->io.seek(r->io.ctx, in->data_offset + offset)) {
        return false;
    }
    if (r->io.read(r->io.ctx, r->raw, want) != want) {
        return false;
    }
    r->block_frames = (uint16_t)oh_ima_decode_block(r->raw, want, in->channels, r->block, OH_WAV_MAX_BLOCK_FRAMES);
    return r->block_frames > 0;
}

bool oh_wav_reader_seek(oh_wav_reader_t *r, uint32_t frame)
{
    if (frame > r->info.frames) {
        frame = r->info.frames;
    }
    r->frame = frame;
    if (r->info.format == OH_WAV_PCM16) {
        return r->io.seek(r->io.ctx, r->info.data_offset + frame * r->info.block_align);
    }
    if (frame == r->info.frames) {
        r->block_frames = 0;
        return true;
    }
    if (!load_block(r, frame / r->info.frames_per_block)) {
        return false;
    }
    r->block_pos = (uint16_t)(frame % r->info.frames_per_block);
    return true;
}

size_t oh_wav_reader_read(oh_wav_reader_t *r, int16_t *frames, size_t n)
{
    const oh_wav_info_t *in = &r->info;
    if (r->frame >= in->frames) {
        return 0;
    }
    if ((uint64_t)r->frame + n > in->frames) {
        n = in->frames - r->frame;
    }
    if (in->format == OH_WAV_PCM16) {
        size_t done = 0;
        while (done < n) {
            size_t chunk = n - done;
            const size_t max = sizeof(r->raw) / in->block_align;
            if (chunk > max) {
                chunk = max;
            }
            const size_t bytes = chunk * in->block_align;
            const size_t got = r->io.read(r->io.ctx, r->raw, bytes) / in->block_align;
            for (size_t i = 0; i < got * in->channels; i++) {
                frames[done * in->channels + i] = (int16_t)le16(r->raw + 2 * i);
            }
            done += got;
            r->frame += (uint32_t)got;
            if (got < chunk) {
                break;
            }
        }
        return done;
    }
    size_t done = 0;
    while (done < n) {
        if (r->block_frames == 0 || r->block_pos >= r->block_frames) {
            if (!load_block(r, r->frame / in->frames_per_block)) {
                break;
            }
            r->block_pos = (uint16_t)(r->frame % in->frames_per_block);
            if (r->block_pos >= r->block_frames) {
                break;
            }
        }
        size_t take = r->block_frames - r->block_pos;
        if (take > n - done) {
            take = n - done;
        }
        memcpy(frames + done * in->channels, r->block + (size_t)r->block_pos * in->channels,
               take * in->channels * sizeof(int16_t));
        r->block_pos = (uint16_t)(r->block_pos + take);
        r->frame += (uint32_t)take;
        done += take;
    }
    return done;
}
