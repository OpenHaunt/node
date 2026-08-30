/*
 * What this node thinks the console's clock reads.
 *
 * A shape is only as good as the clock under it. These pin the three awkward cases:
 * a smoothed estimate, a retained message that must not correct one, and a broker
 * restart that must not be smoothed through.
 */
#include "oh_clock_sync.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void the_first_live_sample_is_taken_outright_and_later_ones_are_smoothed(void)
{
    oh_clock_sync_t s;
    oh_clock_sync_init(&s);
    TEST_ASSERT_FALSE(oh_clock_sync_locked(&s));

    /* The console is 1000 ms ahead of this node's clock. */
    oh_clock_sync_feed(&s, 11000, 1, 10000, false);
    TEST_ASSERT_TRUE(oh_clock_sync_locked(&s));
    TEST_ASSERT_EQUAL_INT64(11000, oh_clock_sync_now(&s, 10000));

    /* A sample that disagrees by 100 ms moves the estimate a fifth of the way, not
     * all of it: what is being corrected is one-way latency, which varies, and a
     * jump would jog every running effect by however much it varied. */
    oh_clock_sync_feed(&s, 12100, 2, 11000, false);
    TEST_ASSERT_EQUAL_INT64(1020, oh_clock_sync_now(&s, 0));

    oh_clock_sync_feed(&s, 13100, 3, 12000, false);
    TEST_ASSERT_EQUAL_INT64(1036, oh_clock_sync_now(&s, 0));
}

/*
 * A retained message was published at an unknown time in the past — the broker hands
 * it over on subscribe, and it may be a second old or an hour. It seeds an empty
 * estimate, because a rough answer beats none, and never corrects a live one.
 */
static void a_retained_sample_seeds_but_never_corrects(void)
{
    oh_clock_sync_t s;
    oh_clock_sync_init(&s);

    oh_clock_sync_feed(&s, 11000, 1, 10000, true);
    TEST_ASSERT_EQUAL_INT64(1000, oh_clock_sync_now(&s, 0));

    oh_clock_sync_feed(&s, 90000, 2, 10000, true);
    TEST_ASSERT_EQUAL_INT64(1000, oh_clock_sync_now(&s, 0));

    /* A live one does correct it. Five hundred milliseconds out is more than one
     * step may cover, so it moves by the slew limit and would reach the rest over
     * the next few seconds. */
    oh_clock_sync_feed(&s, 11500, 3, 10000, false);
    TEST_ASSERT_EQUAL_INT64(1000 + OH_CLOCK_MAX_SLEW_MS, oh_clock_sync_now(&s, 0));
}

/*
 * The broker restarted and replayed its retained clock, so `seq` went backwards.
 * Smoothing towards that would drag the estimate through a number from before the
 * gap; starting again is the only honest answer.
 */
static void a_sequence_that_goes_backwards_starts_the_estimate_again(void)
{
    oh_clock_sync_t s;
    oh_clock_sync_init(&s);
    oh_clock_sync_feed(&s, 11000, 40, 10000, false);
    oh_clock_sync_feed(&s, 12000, 41, 11000, false);
    TEST_ASSERT_EQUAL_INT64(1000, oh_clock_sync_now(&s, 0));

    oh_clock_sync_feed(&s, 20500, 1, 20000, false);
    TEST_ASSERT_EQUAL_INT64(500, oh_clock_sync_now(&s, 0));
}

/*
 * One wildly late message must not step every running effect by its whole error at
 * once. A genuine large offset still arrives — just over a few seconds, which is
 * imperceptible, rather than as a visible jolt.
 */
static void a_large_correction_arrives_gradually_rather_than_as_a_jolt(void)
{
    oh_clock_sync_t s;
    oh_clock_sync_init(&s);
    oh_clock_sync_feed(&s, 10000, 1, 10000, false);
    TEST_ASSERT_EQUAL_INT64(0, oh_clock_sync_now(&s, 0));

    /* Ten seconds out: a fifth of that is two seconds, and the slew limit holds it
     * to fifty milliseconds. */
    oh_clock_sync_feed(&s, 20000, 2, 10000, false);
    TEST_ASSERT_EQUAL_INT64(OH_CLOCK_MAX_SLEW_MS, oh_clock_sync_now(&s, 0));

    oh_clock_sync_feed(&s, 20000, 3, 10000, false);
    TEST_ASSERT_EQUAL_INT64(2 * OH_CLOCK_MAX_SLEW_MS, oh_clock_sync_now(&s, 0));
}

/* Before any sample has arrived a node has no better idea than its own clock. It
 * renders against that rather than refusing to render, and the first sample fixes
 * it — which is why a retained clock is worth publishing at all. */
static void with_no_sample_yet_the_nodes_own_clock_is_the_answer(void)
{
    oh_clock_sync_t s;
    oh_clock_sync_init(&s);
    TEST_ASSERT_EQUAL_INT64(5000, oh_clock_sync_now(&s, 5000));
    TEST_ASSERT_FALSE(oh_clock_sync_locked(&s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(the_first_live_sample_is_taken_outright_and_later_ones_are_smoothed);
    RUN_TEST(a_retained_sample_seeds_but_never_corrects);
    RUN_TEST(a_sequence_that_goes_backwards_starts_the_estimate_again);
    RUN_TEST(a_large_correction_arrives_gradually_rather_than_as_a_jolt);
    RUN_TEST(with_no_sample_yet_the_nodes_own_clock_is_the_answer);
    return UNITY_END();
}
