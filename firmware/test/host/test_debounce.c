#include "oh_debounce.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void a_bounce_train_collapses_to_one_edge_with_the_first_timestamp(void)
{
    oh_debounce_t d;
    oh_debounce_init(&d, false, 20);
    bool level;
    int64_t ts;
    /* Contact closes at t=1000us and chatters for 5 ms. */
    TEST_ASSERT_FALSE(oh_debounce_feed(&d, true, 1000, &level, &ts));
    TEST_ASSERT_FALSE(oh_debounce_feed(&d, false, 1500, &level, &ts));
    TEST_ASSERT_FALSE(oh_debounce_feed(&d, true, 2000, &level, &ts));
    TEST_ASSERT_FALSE(oh_debounce_feed(&d, true, 6000, &level, &ts));
    /* Timer check after the settle time. */
    TEST_ASSERT_EQUAL_INT64(2000 + 20000, oh_debounce_due(&d));
    TEST_ASSERT_TRUE(oh_debounce_feed(&d, true, 22500, &level, &ts));
    TEST_ASSERT_TRUE(level);
    /* The bounce back to the old level reset the edge, so it is 2000 not 1000. */
    TEST_ASSERT_EQUAL_INT64(2000, ts);
    TEST_ASSERT_EQUAL_INT64(-1, oh_debounce_due(&d));
}

static void a_glitch_shorter_than_settle_is_nothing(void)
{
    oh_debounce_t d;
    oh_debounce_init(&d, false, 20);
    TEST_ASSERT_FALSE(oh_debounce_feed(&d, true, 0, NULL, NULL));
    TEST_ASSERT_FALSE(oh_debounce_feed(&d, false, 5000, NULL, NULL));
    TEST_ASSERT_EQUAL_INT64(-1, oh_debounce_due(&d));
    TEST_ASSERT_FALSE(oh_debounce_feed(&d, false, 50000, NULL, NULL));
}

static void zero_settle_emits_immediately(void)
{
    oh_debounce_t d;
    oh_debounce_init(&d, true, 0);
    bool level;
    int64_t ts;
    TEST_ASSERT_TRUE(oh_debounce_feed(&d, false, 77, &level, &ts));
    TEST_ASSERT_FALSE(level);
    TEST_ASSERT_EQUAL_INT64(77, ts);
}

static void a_long_press_is_two_edges(void)
{
    oh_debounce_t d;
    oh_debounce_init(&d, false, 10);
    bool level;
    int64_t ts;
    TEST_ASSERT_FALSE(oh_debounce_feed(&d, true, 0, &level, &ts));
    TEST_ASSERT_TRUE(oh_debounce_feed(&d, true, 10000, &level, &ts));
    TEST_ASSERT_TRUE(level);
    TEST_ASSERT_FALSE(oh_debounce_feed(&d, false, 500000, &level, &ts));
    TEST_ASSERT_TRUE(oh_debounce_feed(&d, false, 511000, &level, &ts));
    TEST_ASSERT_FALSE(level);
    TEST_ASSERT_EQUAL_INT64(500000, ts);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_bounce_train_collapses_to_one_edge_with_the_first_timestamp);
    RUN_TEST(a_glitch_shorter_than_settle_is_nothing);
    RUN_TEST(zero_settle_emits_immediately);
    RUN_TEST(a_long_press_is_two_edges);
    return UNITY_END();
}
