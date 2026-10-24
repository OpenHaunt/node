/*
 * A stored clip: a RIFF WAVE file, 16-bit PCM or IMA ADPCM, mono or stereo, any
 * rate. OpenHaunt/node doc 13 says why those two and not MP3.
 *
 * The reader pulls bytes through an `oh_io_t`, so the same code reads a file on the
 * node's FAT partition and a buffer in a host test.
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "oh_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OH_WAV_PCM16 1u
#define OH_WAV_IMA_ADPCM 0x11u

/* The largest ADPCM block this node decodes. ffmpeg's adpcm_ima_wav writes 512 to
 * 2048 bytes per block depending on rate and channels; bigger is refused, because the
 * decoded block is held per voice. */
#define OH_WAV_MAX_BLOCK 2048u
/* 2048 bytes of mono IMA ADPCM: 4 header bytes, then two samples a byte, plus the
 * header's own sample. */
#define OH_WAV_MAX_BLOCK_FRAMES 4089u

typedef struct {
    uint16_t format; /* OH_WAV_PCM16 or OH_WAV_IMA_ADPCM */
    uint8_t channels;
    uint32_t rate;
    uint16_t block_align;      /* bytes per block (ADPCM) or per frame (PCM) */
    uint16_t frames_per_block; /* 1 for PCM */
    uint32_t data_offset;      /* where the sample data starts in the file */
    uint32_t data_bytes;
    uint32_t frames; /* frames in the clip */
} oh_wav_info_t;

/*
 * Read a WAVE header from the start of a file. The `data` chunk has to begin within
 * `len` bytes. Rejects anything this node cannot play, with a reason in `err`.
 * `file_len`, when non-zero, bounds the data chunk: a truncated upload is not a clip.
 */
bool oh_wav_parse(const uint8_t *buf, size_t len, uint32_t file_len, oh_wav_info_t *out, char err[OH_ERR_MAX]);

const char *oh_wav_format_str(uint16_t format);
uint32_t oh_wav_duration_ms(const oh_wav_info_t *info);

typedef struct {
    size_t (*read)(void *ctx, void *buf, size_t n);
    bool (*seek)(void *ctx, uint32_t offset);
    void *ctx;
} oh_io_t;

/* Decoding one clip. Big (a decoded block), so allocate it rather than put it on a stack. */
typedef struct {
    oh_wav_info_t info;
    oh_io_t io;
    uint32_t frame; /* the next frame read() delivers */
    /* ADPCM: the decoded block `frame` is in, and where in it. */
    int16_t block[OH_WAV_MAX_BLOCK_FRAMES * 2];
    uint16_t block_frames;
    uint16_t block_pos;
    uint8_t raw[OH_WAV_MAX_BLOCK];
} oh_wav_reader_t;

void oh_wav_reader_init(oh_wav_reader_t *r, const oh_wav_info_t *info, const oh_io_t *io);

/* Position at `frame` (clamped to the end). False on an I/O failure. */
bool oh_wav_reader_seek(oh_wav_reader_t *r, uint32_t frame);

/* Up to `n` frames, interleaved by info.channels, in host order. Fewer at the end of
 * the clip or on an I/O failure; 0 means nothing more. */
size_t oh_wav_reader_read(oh_wav_reader_t *r, int16_t *frames, size_t n);

/*
 * Decode one IMA ADPCM block (the WAVE/Microsoft layout: a 4-byte header per channel,
 * then 4-byte groups of eight nibbles per channel, low nibble first). Answers frames
 * decoded, at most `max_frames`.
 */
size_t oh_ima_decode_block(const uint8_t *block, size_t len, uint8_t channels, int16_t *out, size_t max_frames);

#ifdef __cplusplus
}
#endif
