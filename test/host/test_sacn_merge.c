#include <string.h>

#include "oh_sacn_merge.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t data[512];

static oh_e131_frame_t frame(uint8_t who, uint8_t priority, uint8_t level, uint16_t n)
{
    oh_e131_frame_t f;
    memset(&f, 0, sizeof(f));
    memset(f.cid, who, 16);
    f.priority = priority;
    f.universe = 1;
    memset(data, level, sizeof(data));
    f.slots = data;
    f.n_slots = n;
    return f;
}

static void the_highest_priority_wins_outright(void)
{
    oh_sacn_merge_t m;
    oh_sacn_merge_init(&m);
    uint8_t a[512], out[512];
    oh_e131_frame_t f = frame(1, 100, 10, 512);
    memcpy(a, data, sizeof(a));
    f.slots = a;
    TEST_ASSERT_TRUE(oh_sacn_merge_feed(&m, &f, 0));
    oh_e131_frame_t g = frame(2, 90, 200, 512);
    TEST_ASSERT_TRUE(oh_sacn_merge_feed(&m, &g, 0));
    TEST_ASSERT_EQUAL(512, oh_sacn_merge_output(&m, out));
    TEST_ASSERT_EQUAL(10, out[0]); /* the brighter, lower-priority source loses */
}

static void equal_priorities_merge_highest_takes_precedence(void)
{
    oh_sacn_merge_t m;
    oh_sacn_merge_init(&m);
    uint8_t a[512], out[512];
    oh_e131_frame_t f = frame(1, 100, 0, 4);
    a[0] = 50, a[1] = 0, a[2] = 7, a[3] = 0;
    f.slots = a;
    oh_sacn_merge_feed(&m, &f, 0);
    oh_e131_frame_t g = frame(2, 100, 0, 3);
    data[0] = 10, data[1] = 60, data[2] = 0;
    oh_sacn_merge_feed(&m, &g, 0);
    TEST_ASSERT_EQUAL(4, oh_sacn_merge_output(&m, out));
    TEST_ASSERT_EQUAL(50, out[0]);
    TEST_ASSERT_EQUAL(60, out[1]);
    TEST_ASSERT_EQUAL(7, out[2]);
}

static void a_silent_source_goes_after_two_and_a_half_seconds(void)
{
    oh_sacn_merge_t m;
    oh_sacn_merge_init(&m);
    uint8_t out[512];
    oh_e131_frame_t f = frame(1, 100, 9, 512);
    oh_sacn_merge_feed(&m, &f, 1000);
    TEST_ASSERT_FALSE(oh_sacn_merge_expire(&m, 3499));
    TEST_ASSERT_EQUAL(1, oh_sacn_merge_sources(&m));
    TEST_ASSERT_TRUE(oh_sacn_merge_expire(&m, 3500));
    TEST_ASSERT_EQUAL(0, oh_sacn_merge_output(&m, out));
}

static void a_terminated_stream_goes_at_once_and_a_preview_never_arrives(void)
{
    oh_sacn_merge_t m;
    oh_sacn_merge_init(&m);
    oh_e131_frame_t f = frame(1, 100, 9, 512);
    oh_sacn_merge_feed(&m, &f, 0);
    f.options = OH_E131_OPT_TERMINATED;
    TEST_ASSERT_FALSE(oh_sacn_merge_feed(&m, &f, 10));
    TEST_ASSERT_EQUAL(0, oh_sacn_merge_sources(&m));
    oh_e131_frame_t p = frame(2, 100, 9, 512);
    p.options = OH_E131_OPT_PREVIEW;
    TEST_ASSERT_FALSE(oh_sacn_merge_feed(&m, &p, 10));
    TEST_ASSERT_EQUAL(0, oh_sacn_merge_sources(&m));
}

static void a_fifth_source_takes_the_place_of_a_weaker_one_only(void)
{
    oh_sacn_merge_t m;
    oh_sacn_merge_init(&m);
    for (uint8_t i = 1; i <= 4; i++) {
        oh_e131_frame_t f = frame(i, (uint8_t)(100 + i), 1, 512);
        TEST_ASSERT_TRUE(oh_sacn_merge_feed(&m, &f, 0));
    }
    oh_e131_frame_t weak = frame(9, 50, 1, 512);
    TEST_ASSERT_FALSE(oh_sacn_merge_feed(&m, &weak, 0));
    oh_e131_frame_t strong = frame(10, 150, 77, 512);
    TEST_ASSERT_TRUE(oh_sacn_merge_feed(&m, &strong, 0));
    uint8_t out[512];
    oh_sacn_merge_output(&m, out);
    TEST_ASSERT_EQUAL(77, out[0]);
    TEST_ASSERT_EQUAL(4, oh_sacn_merge_sources(&m));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(the_highest_priority_wins_outright);
    RUN_TEST(equal_priorities_merge_highest_takes_precedence);
    RUN_TEST(a_silent_source_goes_after_two_and_a_half_seconds);
    RUN_TEST(a_terminated_stream_goes_at_once_and_a_preview_never_arrives);
    RUN_TEST(a_fifth_source_takes_the_place_of_a_weaker_one_only);
    return UNITY_END();
}
