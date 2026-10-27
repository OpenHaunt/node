#include "oh_rtp.h"

#include <string.h>

bool oh_rtp_parse(const uint8_t *buf, size_t len, oh_rtp_packet_t *out)
{
    if (len < 12 || (buf[0] >> 6) != 2) {
        return false;
    }
    const bool padding = buf[0] & 0x20;
    const bool extension = buf[0] & 0x10;
    const size_t csrc = buf[0] & 0x0F;
    size_t at = 12 + 4 * csrc;
    if (at > len) {
        return false;
    }
    if (extension) {
        if (at + 4 > len) {
            return false;
        }
        const size_t words = ((size_t)buf[at + 2] << 8) | buf[at + 3];
        at += 4 + 4 * words;
        if (at > len) {
            return false;
        }
    }
    size_t end = len;
    if (padding) {
        const size_t pad = buf[len - 1];
        if (pad == 0 || at + pad > len) {
            return false;
        }
        end -= pad;
    }
    out->marker = buf[1] & 0x80;
    out->pt = buf[1] & 0x7F;
    out->seq = (uint16_t)((buf[2] << 8) | buf[3]);
    out->ts = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 8) | buf[7];
    out->ssrc = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16) | ((uint32_t)buf[10] << 8) | buf[11];
    out->payload = buf + at;
    out->payload_len = end - at;
    return true;
}

/* ── Jitter buffer ───────────────────────────────────────────────────── */

void oh_jitter_init(oh_jitter_t *j, int16_t *frames, uint32_t *tags, uint32_t *valid, uint32_t capacity,
                    uint8_t channels)
{
    memset(j, 0, sizeof(*j));
    j->frames = frames;
    j->tags = tags;
    j->valid = valid;
    j->capacity = capacity - capacity % OH_JITTER_BLOCK;
    j->channels = channels < 1 ? 1 : (channels > 2 ? 2 : channels);
    oh_jitter_reset(j, 0);
}

void oh_jitter_reset(oh_jitter_t *j, uint32_t ts)
{
    const uint32_t blocks = j->capacity / OH_JITTER_BLOCK;
    for (uint32_t b = 0; b < blocks; b++) {
        j->tags[b] = UINT32_MAX;
    }
    memset(j->valid, 0, ((j->capacity + 31) / 32) * sizeof(uint32_t));
    j->have = false;
    /* Widen around `ts` in the middle of the 64-bit range's low half, so a
     * timestamp a little before it does not go negative. */
    j->newest = ((uint64_t)1 << 32) + ts;
    j->first = j->newest;
    j->cursor = 0;
    j->late = 0;
}

uint64_t oh_jitter_widen(const oh_jitter_t *j, uint32_t ts)
{
    const uint32_t low = (uint32_t)j->newest;
    return j->newest + (int64_t)(int32_t)(ts - low);
}

static void claim_block(oh_jitter_t *j, uint64_t block)
{
    const uint32_t slot = (uint32_t)(block % (j->capacity / OH_JITTER_BLOCK));
    if (j->tags[slot] == (uint32_t)block) {
        return;
    }
    j->tags[slot] = (uint32_t)block;
    const uint32_t first = slot * OH_JITTER_BLOCK;
    for (uint32_t f = first; f < first + OH_JITTER_BLOCK; f++) {
        j->valid[f / 32] &= ~(1u << (f % 32));
    }
}

static bool held(const oh_jitter_t *j, uint64_t t)
{
    int16_t f[2];
    return oh_jitter_get(j, t, f);
}

/* Every frame of [from, to) arrived once already: a second copy of it, from the
 * other of two paths, rather than something late. */
static bool all_held(const oh_jitter_t *j, uint64_t from, uint64_t to)
{
    for (uint64_t t = from; t < to; t++) {
        if (!held(j, t)) {
            return false;
        }
    }
    return true;
}

void oh_jitter_write_l16be(oh_jitter_t *j, uint64_t ext, const uint8_t *payload, size_t n)
{
    if (j->capacity == 0 || n == 0) {
        return;
    }
    const uint64_t end = ext + n;
    /* Behind the reader, or so far ahead it would overwrite what is still to play. */
    const uint64_t floor = j->cursor;
    const uint64_t ceiling = (j->cursor ? j->cursor : ext) + j->capacity;
    if (end <= floor) {
        if (!all_held(j, ext, end)) {
            j->late++;
        }
        return;
    }
    if (ext >= ceiling) {
        j->late++;
        return;
    }
    size_t skip = 0;
    if (ext < floor) {
        skip = (size_t)(floor - ext);
        if (!all_held(j, ext, floor)) {
            j->late++;
        }
    }
    size_t last = n;
    if (end > ceiling) {
        last = (size_t)(ceiling - ext);
        j->late++;
    }
    for (size_t i = skip; i < last; i++) {
        const uint64_t t = ext + i;
        claim_block(j, t / OH_JITTER_BLOCK);
        const uint32_t f = (uint32_t)(t % j->capacity);
        for (uint8_t c = 0; c < j->channels; c++) {
            const uint8_t *s = payload + (i * j->channels + c) * 2;
            j->frames[f * j->channels + c] = (int16_t)((s[0] << 8) | s[1]);
        }
        j->valid[f / 32] |= 1u << (f % 32);
    }
    if (!j->have || ext + skip < j->first) {
        j->first = ext + skip;
    }
    if (!j->have || ext + last > j->newest) {
        j->newest = ext + last;
    }
    j->have = true;
}

bool oh_jitter_get(const oh_jitter_t *j, uint64_t ext, int16_t out[2])
{
    if (!j->have || j->capacity == 0) {
        return false;
    }
    const uint64_t block = ext / OH_JITTER_BLOCK;
    const uint32_t slot = (uint32_t)(block % (j->capacity / OH_JITTER_BLOCK));
    if (j->tags[slot] != (uint32_t)block) {
        return false;
    }
    const uint32_t f = (uint32_t)(ext % j->capacity);
    if (!(j->valid[f / 32] & (1u << (f % 32)))) {
        return false;
    }
    out[0] = j->frames[f * j->channels];
    out[1] = j->channels == 2 ? j->frames[f * j->channels + 1] : out[0];
    return true;
}

void oh_jitter_consume(oh_jitter_t *j, uint64_t ext)
{
    if (ext > j->cursor) {
        j->cursor = ext;
    }
}

uint32_t oh_jitter_depth(const oh_jitter_t *j)
{
    if (!j->have || j->newest <= j->cursor) {
        return 0;
    }
    const uint64_t d = j->newest - (j->cursor > j->first ? j->cursor : j->first);
    return d > j->capacity ? j->capacity : (uint32_t)d;
}
