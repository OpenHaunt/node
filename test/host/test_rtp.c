/*
 * RTP in, samples out: the header, the 32-bit wrap, a lap of the ring, and the
 * packets that arrive too late or too early.
 */
#include <string.h>

#include "oh_rtp.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define CAP 4096
static int16_t frames[CAP * 2];
static uint32_t tags[CAP / OH_JITTER_BLOCK];
static uint32_t valid[CAP / 32];

static size_t packet(uint8_t *p, uint32_t ts, uint16_t seq, const int16_t *samples, size_t n)
{
    p[0] = 0x80;
    p[1] = 96;
    p[2] = (uint8_t)(seq >> 8);
    p[3] = (uint8_t)seq;
    p[4] = (uint8_t)(ts >> 24);
    p[5] = (uint8_t)(ts >> 16);
    p[6] = (uint8_t)(ts >> 8);
    p[7] = (uint8_t)ts;
    memset(p + 8, 0x11, 4);
    for (size_t i = 0; i < n; i++) {
        p[12 + 2 * i] = (uint8_t)((uint16_t)samples[i] >> 8);
        p[13 + 2 * i] = (uint8_t)samples[i];
    }
    return 12 + 2 * n;
}

static void a_header_with_csrcs_an_extension_and_padding_is_read_past(void)
{
    uint8_t p[64] = {0xB2, 0xE0, 0x12, 0x34, 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    /* Two CSRCs, then an extension of one word, then four payload bytes, then two of padding. */
    size_t at = 12 + 8;
    p[at + 2] = 0;
    p[at + 3] = 1;
    at += 8;
    memcpy(p + at, "\x7F\xFF\x80\x00", 4);
    at += 4;
    p[at++] = 0;
    p[at++] = 2;
    oh_rtp_packet_t r;
    TEST_ASSERT_TRUE(oh_rtp_parse(p, at, &r));
    TEST_ASSERT_TRUE(r.marker);
    TEST_ASSERT_EQUAL_UINT8(96, r.pt);
    TEST_ASSERT_EQUAL_UINT16(0x1234, r.seq);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, r.ts);
    TEST_ASSERT_EQUAL_HEX32(0x01020304, r.ssrc);
    TEST_ASSERT_EQUAL_size_t(4, r.payload_len);
    TEST_ASSERT_EQUAL_UINT8(0x7F, r.payload[0]);

    p[0] = 0x40; /* version 1 */
    TEST_ASSERT_FALSE(oh_rtp_parse(p, at, &r));
    TEST_ASSERT_FALSE(oh_rtp_parse(p, 8, &r));
}

static void samples_come_back_by_timestamp_across_the_32_bit_wrap(void)
{
    oh_jitter_t j;
    oh_jitter_init(&j, frames, tags, valid, CAP, 1);
    const uint32_t ts0 = 0xFFFFFF00u;
    oh_jitter_reset(&j, ts0);
    uint8_t p[1500];
    int16_t s[240];
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < 240; i++) {
            s[i] = (int16_t)(k * 1000 + i);
        }
        const uint32_t ts = ts0 + (uint32_t)(k * 240);
        oh_rtp_packet_t r;
        TEST_ASSERT_TRUE(oh_rtp_parse(p, packet(p, ts, (uint16_t)k, s, 240), &r));
        oh_jitter_write_l16be(&j, oh_jitter_widen(&j, r.ts), r.payload, r.payload_len / 2);
    }
    const uint64_t base = oh_jitter_widen(&j, ts0);
    int16_t out[2];
    for (int k = 0; k < 4; k++) {
        TEST_ASSERT_TRUE(oh_jitter_get(&j, base + (uint64_t)k * 240 + 17, out));
        TEST_ASSERT_EQUAL_INT16(k * 1000 + 17, out[0]);
        TEST_ASSERT_EQUAL_INT16(out[0], out[1]); /* mono reads on both channels */
    }
    /* The third packet straddles 2^32 and is still the same timeline. */
    TEST_ASSERT_TRUE(oh_jitter_widen(&j, 0x00000010u) > base);
    TEST_ASSERT_EQUAL_UINT32(960, oh_jitter_depth(&j));
    TEST_ASSERT_FALSE(oh_jitter_get(&j, base + 960, out));
}

static void a_lost_packet_is_a_hole_and_a_lap_old_frame_is_not_sound(void)
{
    oh_jitter_t j;
    oh_jitter_init(&j, frames, tags, valid, CAP, 1);
    oh_jitter_reset(&j, 0);
    uint8_t p[1500];
    int16_t s[100];
    for (int i = 0; i < 100; i++) {
        s[i] = 5;
    }
    const uint64_t base = oh_jitter_widen(&j, 0);
    oh_rtp_packet_t r;
    oh_rtp_parse(p, packet(p, 0, 0, s, 100), &r);
    oh_jitter_write_l16be(&j, base, r.payload, 100);
    /* 100..199 lost */
    oh_rtp_parse(p, packet(p, 200, 2, s, 100), &r);
    oh_jitter_write_l16be(&j, base + 200, r.payload, 100);
    int16_t out[2];
    TEST_ASSERT_TRUE(oh_jitter_get(&j, base + 99, out));
    TEST_ASSERT_FALSE(oh_jitter_get(&j, base + 100, out));
    TEST_ASSERT_FALSE(oh_jitter_get(&j, base + 150, out));
    TEST_ASSERT_TRUE(oh_jitter_get(&j, base + 200, out));
    /* The same ring slot one lap later holds nothing until something is written. */
    TEST_ASSERT_FALSE(oh_jitter_get(&j, base + CAP + 50, out));
}

static void behind_the_cursor_or_a_whole_buffer_ahead_is_late(void)
{
    oh_jitter_t j;
    oh_jitter_init(&j, frames, tags, valid, CAP, 2);
    oh_jitter_reset(&j, 1000);
    const uint64_t base = oh_jitter_widen(&j, 1000);
    uint8_t payload[400] = {0};
    oh_jitter_write_l16be(&j, base, payload, 100);
    oh_jitter_consume(&j, base + 500);
    TEST_ASSERT_EQUAL_UINT32(0, j.late);
    oh_jitter_write_l16be(&j, base + 100, payload, 100); /* already played */
    TEST_ASSERT_EQUAL_UINT32(1, j.late);
    oh_jitter_write_l16be(&j, base + 500 + CAP, payload, 100); /* would overwrite what is to come */
    TEST_ASSERT_EQUAL_UINT32(2, j.late);
    oh_jitter_write_l16be(&j, base + 450, payload, 100); /* half of it is still in time */
    TEST_ASSERT_EQUAL_UINT32(3, j.late);
    int16_t out[2];
    TEST_ASSERT_TRUE(oh_jitter_get(&j, base + 520, out));
    TEST_ASSERT_EQUAL_UINT32(50, oh_jitter_depth(&j));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_header_with_csrcs_an_extension_and_padding_is_read_past);
    RUN_TEST(samples_come_back_by_timestamp_across_the_32_bit_wrap);
    RUN_TEST(a_lost_packet_is_a_hole_and_a_lap_old_frame_is_not_sound);
    RUN_TEST(behind_the_cursor_or_a_whole_buffer_ahead_is_late);
    return UNITY_END();
}
