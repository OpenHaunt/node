#include "oh_amp.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define IDLE 2000000

static void a_sound_powers_it_and_quiet_lets_it_go_after_the_idle_time(void)
{
    oh_amp_t a;
    oh_amp_init(&a, IDLE, false);
    TEST_ASSERT_FALSE(oh_amp_step(&a, false, 1000));
    TEST_ASSERT_FALSE(a.on);
    TEST_ASSERT_TRUE(oh_amp_step(&a, true, 2000));
    TEST_ASSERT_TRUE(a.on);
    TEST_ASSERT_FALSE(oh_amp_step(&a, false, 10000));
    TEST_ASSERT_FALSE(oh_amp_step(&a, false, 10000 + IDLE));
    TEST_ASSERT_TRUE(a.on);
    TEST_ASSERT_TRUE(oh_amp_step(&a, false, 10001 + IDLE));
    TEST_ASSERT_FALSE(a.on);
}

static void a_sound_inside_the_idle_time_starts_the_wait_again(void)
{
    oh_amp_t a;
    oh_amp_init(&a, IDLE, false);
    oh_amp_step(&a, true, 0);
    oh_amp_step(&a, false, 1000);
    oh_amp_step(&a, true, 1500000);
    oh_amp_step(&a, false, 1600000);
    TEST_ASSERT_FALSE(oh_amp_step(&a, false, 1000 + IDLE + 1));
    TEST_ASSERT_TRUE(a.on);
}

/* V0.1: a line out into somebody else's amplifier hums when it floats, so it is
 * never let go — and it is on from the first block, before anything has played. */
static void always_on_is_on_from_the_first_block_and_never_off(void)
{
    oh_amp_t a;
    oh_amp_init(&a, IDLE, true);
    TEST_ASSERT_TRUE(oh_amp_step(&a, false, 0));
    TEST_ASSERT_TRUE(a.on);
    TEST_ASSERT_FALSE(oh_amp_step(&a, false, 100 * IDLE));
    TEST_ASSERT_TRUE(a.on);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_sound_powers_it_and_quiet_lets_it_go_after_the_idle_time);
    RUN_TEST(a_sound_inside_the_idle_time_starts_the_wait_again);
    RUN_TEST(always_on_is_on_from_the_first_block_and_never_off);
    return UNITY_END();
}
