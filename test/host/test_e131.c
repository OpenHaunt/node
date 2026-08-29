#include <string.h>

#include "oh_e131.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A minimal but well-formed E1.31 data packet. */
static size_t make_packet(uint8_t *p, uint16_t universe, uint16_t n_slots)
{
    memset(p, 0, 638);
    p[0] = 0x00;
    p[1] = 0x10; /* preamble size */
    memcpy(p + 4, "ASC-E1.17\0\0\0", 12);
    p[21] = 0x04; /* root vector */
    p[43] = 0x02; /* framing vector */
    p[108] = 100; /* priority */
    p[111] = 7;   /* sequence */
    p[113] = (uint8_t)(universe >> 8);
    p[114] = (uint8_t)universe;
    p[117] = 0x02; /* DMP vector */
    uint16_t count = n_slots + 1;
    p[123] = (uint8_t)(count >> 8);
    p[124] = (uint8_t)count;
    p[125] = 0x00; /* start code */
    for (uint16_t i = 0; i < n_slots; i++) {
        p[126 + i] = (uint8_t)(i + 1);
    }
    return 126 + n_slots;
}

static void a_frame_yields_its_universe_and_slots(void)
{
    uint8_t p[638];
    size_t n = make_packet(p, 5, 512);
    oh_e131_frame_t f;
    TEST_ASSERT_TRUE(oh_e131_parse(p, n, &f));
    TEST_ASSERT_EQUAL(5, f.universe);
    TEST_ASSERT_EQUAL(512, f.n_slots);
    TEST_ASSERT_EQUAL(7, f.sequence);
    TEST_ASSERT_EQUAL(100, f.priority);
    TEST_ASSERT_EQUAL(1, f.slots[0]);
    TEST_ASSERT_EQUAL(0, f.slots[255]);
}

static void a_short_universe_is_fine(void)
{
    uint8_t p[638];
    size_t n = make_packet(p, 300, 24);
    oh_e131_frame_t f;
    TEST_ASSERT_TRUE(oh_e131_parse(p, n, &f));
    TEST_ASSERT_EQUAL(300, f.universe);
    TEST_ASSERT_EQUAL(24, f.n_slots);
}

static void a_datagram_that_is_not_e131_is_ignored(void)
{
    oh_e131_frame_t f;
    uint8_t zeros[200] = {0};
    TEST_ASSERT_FALSE(oh_e131_parse((const uint8_t *)"hello", 5, &f));
    TEST_ASSERT_FALSE(oh_e131_parse(zeros, sizeof(zeros), &f));
    uint8_t p[638];
    size_t n = make_packet(p, 1, 512);
    TEST_ASSERT_FALSE(oh_e131_parse(p, n - 10, &f)); /* truncated */
    p[43] = 0x03;                                      /* not a data packet */
    TEST_ASSERT_FALSE(oh_e131_parse(p, n, &f));
}

static void multicast_groups_follow_the_universe(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xEFFF0001, oh_e131_multicast_group(1));
    TEST_ASSERT_EQUAL_HEX32(0xEFFF0100, oh_e131_multicast_group(256));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_frame_yields_its_universe_and_slots);
    RUN_TEST(a_short_universe_is_fine);
    RUN_TEST(a_datagram_that_is_not_e131_is_ignored);
    RUN_TEST(multicast_groups_follow_the_universe);
    return UNITY_END();
}
