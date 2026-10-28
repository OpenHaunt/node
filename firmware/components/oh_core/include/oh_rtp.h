/*
 * RTP (RFC 3550) as far as an L16 stream needs it, and the buffer that turns
 * packets back into a timeline of samples. OpenHaunt/node doc 13, "The RTP stream".
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pt;
    bool marker;
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
    const uint8_t *payload;
    size_t payload_len;
} oh_rtp_packet_t;

/* Version 2 only; CSRCs, a header extension and padding are stepped over. */
bool oh_rtp_parse(const uint8_t *buf, size_t len, oh_rtp_packet_t *out);

/*
 * The stream's samples, indexed by RTP timestamp.
 *
 * Timestamps are widened to 64 bits against the newest one seen, so a session that
 * runs past the 32-bit wrap carries on. Storage is a ring of `capacity` frames;
 * every frame has a valid bit, and every block of OH_JITTER_BLOCK frames records
 * which block of the timeline it holds, so a frame left over from a lap ago reads as
 * missing rather than as the sound of a second ago.
 */
#define OH_JITTER_BLOCK 64u

typedef struct {
    int16_t *frames;  /* capacity * channels */
    uint32_t *tags;   /* capacity / OH_JITTER_BLOCK: which timeline block each slot holds */
    uint32_t *valid;  /* capacity bits */
    uint32_t capacity; /* frames, a multiple of OH_JITTER_BLOCK */
    uint8_t channels;
    bool have;          /* anything written yet */
    uint64_t newest;    /* widened timestamp of the newest frame written + 1 */
    uint64_t first;     /* widened timestamp of the first frame ever written */
    uint64_t cursor;    /* the reader has played everything before this */
    uint32_t late;      /* packets (or parts) that arrived behind the cursor or too far ahead */
} oh_jitter_t;

/* `frames`, `tags` and `valid` are the caller's storage, sized for `capacity`
 * (rounded down to a whole number of blocks). */
void oh_jitter_init(oh_jitter_t *j, int16_t *frames, uint32_t *tags, uint32_t *valid, uint32_t capacity,
                    uint8_t channels);

/* Forget everything, and widen future timestamps around `ts`. */
void oh_jitter_reset(oh_jitter_t *j, uint32_t ts);

/* Widen a 32-bit RTP timestamp to the stream's 64-bit timeline. */
uint64_t oh_jitter_widen(const oh_jitter_t *j, uint32_t ts);

/* Store `n` frames of big-endian L16 starting at widened timestamp `ext`. */
void oh_jitter_write_l16be(oh_jitter_t *j, uint64_t ext, const uint8_t *payload, size_t n);

/* One frame, if it arrived. */
bool oh_jitter_get(const oh_jitter_t *j, uint64_t ext, int16_t out[2]);

/* The reader is done with everything before `ext`. */
void oh_jitter_consume(oh_jitter_t *j, uint64_t ext);

/* Frames buffered ahead of the cursor. */
uint32_t oh_jitter_depth(const oh_jitter_t *j);

/*
 * What the sequence numbers say about the packets that reached the receiver.
 *
 * The buffer's `missing` says a frame was not there when it was played; this says
 * whether its packet ever reached the socket's reader. A gap here is a packet lost on
 * the way to it — on the wire, or dropped by the stack because its queue was full —
 * and a gap the buffer reports with none here was lost after it. A packet that comes
 * in behind a gap fills it again (counted as reordered, not lost), and a second copy
 * of one — a redundant stream's other path — is a duplicate.
 */
typedef struct {
    bool have;
    uint16_t last;       /* the newest sequence number seen */
    uint32_t packets;    /* accepted packets */
    uint32_t lost;       /* skipped by the sequence and never seen since */
    uint32_t reordered;  /* arrived behind a newer one */
    uint32_t duplicates; /* arrived twice */
} oh_rtp_seq_t;

void oh_rtp_seq_reset(oh_rtp_seq_t *t);

/* Note one packet. Answers how many it found skipped before it: 0 in order. */
uint32_t oh_rtp_seq_note(oh_rtp_seq_t *t, uint16_t seq);

#ifdef __cplusplus
}
#endif
