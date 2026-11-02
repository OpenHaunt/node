/*
 * Building WAV files in memory for the audio tests: a PCM16 writer, a reference
 * IMA ADPCM encoder in the WAVE layout, and an oh_io_t over a buffer.
 */
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "oh_wav.h"

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t at;
} mem_io_t;

static size_t mem_read(void *ctx, void *dst, size_t n)
{
    mem_io_t *m = ctx;
    if (m->at >= m->len) {
        return 0;
    }
    if (n > m->len - m->at) {
        n = m->len - m->at;
    }
    memcpy(dst, m->buf + m->at, n);
    m->at += n;
    return n;
}

static bool mem_seek(void *ctx, uint32_t offset)
{
    mem_io_t *m = ctx;
    if (offset > m->len) {
        return false;
    }
    m->at = offset;
    return true;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

/* A 44-byte header, plus an optional LIST chunk ahead of data to prove it is skipped. */
static size_t wav_header(uint8_t *out, uint16_t format, uint8_t ch, uint32_t rate, uint16_t block_align,
                         uint16_t bits, uint32_t data_bytes, bool with_list)
{
    size_t at = 0;
    memcpy(out, "RIFF", 4);
    memcpy(out + 8, "WAVE", 4);
    at = 12;
    memcpy(out + at, "fmt ", 4);
    put32(out + at + 4, 16);
    put16(out + at + 8, format);
    put16(out + at + 10, ch);
    put32(out + at + 12, rate);
    put32(out + at + 16, rate * block_align);
    put16(out + at + 20, block_align);
    put16(out + at + 22, bits);
    at += 24;
    if (with_list) {
        memcpy(out + at, "LIST", 4);
        put32(out + at + 4, 5); /* odd: the pad byte must be skipped too */
        memcpy(out + at + 8, "INFOx", 5);
        out[at + 13] = 0;
        at += 14;
    }
    memcpy(out + at, "data", 4);
    put32(out + at + 4, data_bytes);
    at += 8;
    put32(out + 4, (uint32_t)(at - 8 + data_bytes));
    return at;
}

/* A mono or stereo PCM16 file of `frames` frames; sample = f(frame, channel). */
static uint8_t *wav_pcm16(uint32_t rate, uint8_t ch, uint32_t frames, int16_t (*f)(uint32_t, uint8_t), size_t *len)
{
    uint8_t *w = malloc(64 + frames * ch * 2u);
    const size_t h = wav_header(w, OH_WAV_PCM16, ch, rate, (uint16_t)(2 * ch), 16, frames * ch * 2u, true);
    for (uint32_t i = 0; i < frames; i++) {
        for (uint8_t c = 0; c < ch; c++) {
            put16(w + h + (i * ch + c) * 2u, (uint16_t)f(i, c));
        }
    }
    *len = h + frames * ch * 2u;
    return w;
}

/* ── A reference IMA ADPCM encoder ── */

static const int16_t enc_step[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t enc_index[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static uint8_t enc_nibble(int32_t *pred, int32_t *index, int16_t sample)
{
    const int32_t step = enc_step[*index];
    int32_t diff = sample - *pred;
    uint8_t n = 0;
    if (diff < 0) {
        n = 8;
        diff = -diff;
    }
    int32_t delta = step >> 3;
    if (diff >= step) {
        n |= 4;
        diff -= step;
        delta += step;
    }
    if (diff >= step >> 1) {
        n |= 2;
        diff -= step >> 1;
        delta += step >> 1;
    }
    if (diff >= step >> 2) {
        n |= 1;
        delta += step >> 2;
    }
    *pred += (n & 8) ? -delta : delta;
    if (*pred > 32767) {
        *pred = 32767;
    } else if (*pred < -32768) {
        *pred = -32768;
    }
    *index += enc_index[n];
    if (*index < 0) {
        *index = 0;
    } else if (*index > 88) {
        *index = 88;
    }
    return n;
}

/* Encode `frames` frames as whole blocks of `block_align` bytes (the last one short). */
static uint8_t *wav_ima(uint32_t rate, uint8_t ch, uint32_t frames, uint16_t block_align,
                        int16_t (*f)(uint32_t, uint8_t), size_t *len)
{
    const uint32_t fpb = 1 + ((block_align - 4u * ch) / (4u * ch)) * 8u;
    const uint32_t blocks = (frames + fpb - 1) / fpb;
    uint8_t *w = calloc(1, 64 + (size_t)blocks * block_align);
    uint8_t *d = w + 64;
    size_t bytes = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        const uint32_t base = b * fpb;
        uint8_t *blk = d + bytes;
        int32_t pred[2], index[2] = {0, 0};
        for (uint8_t c = 0; c < ch; c++) {
            pred[c] = f(base, c);
            put16(blk + 4u * c, (uint16_t)pred[c]);
            blk[4u * c + 2] = 0;
            blk[4u * c + 3] = 0;
        }
        size_t at = 4u * ch;
        uint32_t i = 1;
        while (i < fpb && base + i < frames) {
            for (uint8_t c = 0; c < ch; c++) {
                for (int k = 0; k < 8; k++) {
                    const uint32_t fi = base + i + (uint32_t)k;
                    const int16_t s = fi < frames ? f(fi, c) : 0;
                    const uint8_t n = enc_nibble(&pred[c], &index[c], s);
                    blk[at + 4u * c + (size_t)(k >> 1)] |= (k & 1) ? (uint8_t)(n << 4) : n;
                }
            }
            at += 4u * ch;
            i += 8;
        }
        bytes += (b + 1 < blocks) ? block_align : at;
    }
    uint8_t hdr[64];
    const size_t h = wav_header(hdr, OH_WAV_IMA_ADPCM, ch, rate, block_align, 4, (uint32_t)bytes, false);
    memmove(w + h, d, bytes);
    memcpy(w, hdr, h);
    *len = h + bytes;
    return w;
}
