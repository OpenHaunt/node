/*
 * The numeric table, on the node.
 *
 * The third of three copies. `pult-backend`'s `model/effects/tests.rs` and
 * `openhaunt-node-sim`'s `motion/tests.rs` assert the same numbers, and none of the
 * three implementations shares code with the others: each was written from the
 * protocol documents. That is the point — three implementations that agree because
 * they were written from one description prove the description is unambiguous.
 *
 * If this table is changed and the other two are not, what happens is a strip that
 * breathes slightly out of step with the console driving it. Nothing goes red, and
 * nobody notices until two nodes are side by side.
 */
#include "oh_curve.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define CLOSE(want, got, what) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, want, got, what)

/* A cycle starts at half and peaks a quarter in. This is the phase convention the
 * wire protocol assumes; a node that started its sine at zero would be a bug. */
static void a_sine_starts_halfway_up_and_peaks_a_quarter_in(void)
{
    CLOSE(0.5f, oh_curve_sample(OH_SHAPE_SINE, 0.0f, 0.5f), "start");
    CLOSE(1.0f, oh_curve_sample(OH_SHAPE_SINE, 0.25f, 0.5f), "peak");
    CLOSE(0.5f, oh_curve_sample(OH_SHAPE_SINE, 0.5f, 0.5f), "back through the middle");
    CLOSE(0.0f, oh_curve_sample(OH_SHAPE_SINE, 0.75f, 0.5f), "trough");
    CLOSE(0.5f, oh_curve_sample(OH_SHAPE_SINE, 1.0f, 0.5f), "round again");
}

static void a_triangle_rises_over_the_first_half_and_falls_over_the_second(void)
{
    CLOSE(0.0f, oh_curve_sample(OH_SHAPE_TRIANGLE, 0.0f, 0.5f), "start");
    CLOSE(0.5f, oh_curve_sample(OH_SHAPE_TRIANGLE, 0.25f, 0.5f), "half way up");
    CLOSE(1.0f, oh_curve_sample(OH_SHAPE_TRIANGLE, 0.5f, 0.5f), "peak");
    CLOSE(0.5f, oh_curve_sample(OH_SHAPE_TRIANGLE, 0.75f, 0.5f), "half way down");
}

static void a_square_spends_width_of_its_cycle_high(void)
{
    CLOSE(1.0f, oh_curve_sample(OH_SHAPE_SQUARE, 0.0f, 0.5f), "on at the top");
    CLOSE(1.0f, oh_curve_sample(OH_SHAPE_SQUARE, 0.49f, 0.5f), "still on");
    CLOSE(0.0f, oh_curve_sample(OH_SHAPE_SQUARE, 0.5f, 0.5f), "off at half");
    CLOSE(1.0f, oh_curve_sample(OH_SHAPE_SQUARE, 0.24f, 0.25f), "narrow, on");
    CLOSE(0.0f, oh_curve_sample(OH_SHAPE_SQUARE, 0.26f, 0.25f), "narrow, off");
}

static void the_saws_run_opposite_ways(void)
{
    CLOSE(0.0f, oh_curve_sample(OH_SHAPE_SAW_UP, 0.0f, 0.5f), "up starts low");
    CLOSE(0.75f, oh_curve_sample(OH_SHAPE_SAW_UP, 0.75f, 0.5f), "up rises");
    CLOSE(1.0f, oh_curve_sample(OH_SHAPE_SAW_DOWN, 0.0f, 0.5f), "down starts high");
    CLOSE(0.25f, oh_curve_sample(OH_SHAPE_SAW_DOWN, 0.75f, 0.5f), "down falls");
}

static void every_shape_stays_inside_its_range(void)
{
    for (int shape = 0; shape < OH_SHAPE_COUNT; shape++) {
        for (int step = 0; step <= 100; step++) {
            const float level = oh_curve_sample((oh_shape_t)shape, (float)step / 100.0f, 0.5f);
            TEST_ASSERT_TRUE_MESSAGE(level >= 0.0f && level <= 1.0f, "a shape left 0..1");
        }
    }
}

static void every_easing_runs_from_nothing_to_everything(void)
{
    for (int e = 0; e < OH_EASE_COUNT; e++) {
        CLOSE(0.0f, oh_ease((oh_easing_t)e, 0.0f), "start");
        CLOSE(1.0f, oh_ease((oh_easing_t)e, 1.0f), "end");
    }
}

static void an_easing_bends_the_way_its_name_says(void)
{
    CLOSE(0.5f, oh_ease(OH_EASE_LINEAR, 0.5f), "linear");
    TEST_ASSERT_TRUE(oh_ease(OH_EASE_IN, 0.5f) < 0.5f);
    TEST_ASSERT_TRUE(oh_ease(OH_EASE_OUT, 0.5f) > 0.5f);
    CLOSE(0.5f, oh_ease(OH_EASE_IN_OUT, 0.5f), "symmetric");
    CLOSE(0.0f, oh_ease(OH_EASE_STEP, 0.99f), "nothing until it arrives");
}

static void a_one_hertz_cycle_takes_a_second(void)
{
    CLOSE(0.0f, oh_cycle_position(1.0f, false, 0.0f, 1000, 1000), "at the anchor");
    CLOSE(0.25f, oh_cycle_position(1.0f, false, 0.0f, 1000, 1250), "a quarter in");
    CLOSE(0.5f, oh_cycle_position(1.0f, false, 0.0f, 1000, 1500), "half");
    CLOSE(0.0f, oh_cycle_position(1.0f, false, 0.0f, 1000, 2000), "round again");
    CLOSE(0.25f, oh_cycle_position(1.0f, false, 0.0f, 1000, 2250), "and on");
}

static void phase_offsets_where_a_fixture_sits_in_the_cycle(void)
{
    CLOSE(0.0f, oh_cycle_position(1.0f, false, 0.0f, 0, 0), "first");
    CLOSE(0.5f, oh_cycle_position(1.0f, false, 0.5f, 0, 0), "half a cycle behind");
}

static void backward_runs_the_other_way(void)
{
    CLOSE(0.75f, oh_cycle_position(1.0f, true, 0.0f, 0, 250), "backwards");
    CLOSE(0.25f, oh_cycle_position(1.0f, false, 0.0f, 0, 250), "forwards");
}

/* This node's clock and the console's do not agree perfectly, so being asked for a
 * position slightly before an anchor is ordinary rather than exceptional. It has to
 * wrap to the top of the cycle: C's % keeps the sign of the dividend, which would
 * otherwise give a negative phase and a shape running backwards for one tick. */
static void a_position_before_the_anchor_wraps_rather_than_going_negative(void)
{
    CLOSE(0.75f, oh_cycle_position(1.0f, false, 0.0f, 1000, 750), "wrapped");
}

/* A stopped speed master resolves to zero, and every effect on it holds where its
 * phase puts it rather than collapsing to the start of the cycle. */
static void a_rate_of_zero_holds_at_the_phase(void)
{
    CLOSE(0.3f, oh_cycle_position(0.0f, false, 0.3f, 0, 9999999), "held");
}

/*
 * The reason the arithmetic is int64 before it is float.
 *
 * A node left switched on for five hours is eighteen million milliseconds from its
 * anchor. A 24-bit mantissa cannot tell 18,000,000 from 18,000,001, so computing the
 * elapsed time as a float first would stop the effect advancing smoothly — on a node
 * whose only fault was being left on.
 */
static void a_node_that_has_been_up_for_hours_still_lands_on_the_right_millisecond(void)
{
    const int64_t eight_hours = (int64_t)8 * 60 * 60 * 1000;
    CLOSE(0.25f, oh_cycle_position(1.0f, false, 0.0f, 0, eight_hours + 250), "a quarter in");
    CLOSE(0.75f, oh_cycle_position(1.0f, false, 0.0f, 0, eight_hours + 750), "three quarters");

    const int64_t two_days = (int64_t)48 * 60 * 60 * 1000;
    CLOSE(0.5f, oh_cycle_position(2.0f, false, 0.0f, 0, two_days + 250), "and after two days");
}

static void the_wire_spellings_round_trip(void)
{
    for (int i = 0; i < OH_SHAPE_COUNT; i++) {
        oh_shape_t back;
        TEST_ASSERT_TRUE(oh_shape_parse(oh_shape_str((oh_shape_t)i), &back));
        TEST_ASSERT_EQUAL_INT(i, back);
    }
    for (int i = 0; i < OH_EASE_COUNT; i++) {
        oh_easing_t back;
        TEST_ASSERT_TRUE(oh_easing_parse(oh_easing_str((oh_easing_t)i), &back));
        TEST_ASSERT_EQUAL_INT(i, back);
    }
    oh_shape_t ignored;
    TEST_ASSERT_FALSE(oh_shape_parse("spiral", &ignored));
    TEST_ASSERT_FALSE(oh_shape_parse(NULL, &ignored));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_sine_starts_halfway_up_and_peaks_a_quarter_in);
    RUN_TEST(a_triangle_rises_over_the_first_half_and_falls_over_the_second);
    RUN_TEST(a_square_spends_width_of_its_cycle_high);
    RUN_TEST(the_saws_run_opposite_ways);
    RUN_TEST(every_shape_stays_inside_its_range);
    RUN_TEST(every_easing_runs_from_nothing_to_everything);
    RUN_TEST(an_easing_bends_the_way_its_name_says);
    RUN_TEST(a_one_hertz_cycle_takes_a_second);
    RUN_TEST(phase_offsets_where_a_fixture_sits_in_the_cycle);
    RUN_TEST(backward_runs_the_other_way);
    RUN_TEST(a_position_before_the_anchor_wraps_rather_than_going_negative);
    RUN_TEST(a_rate_of_zero_holds_at_the_phase);
    RUN_TEST(a_node_that_has_been_up_for_hours_still_lands_on_the_right_millisecond);
    RUN_TEST(the_wire_spellings_round_trip);
    return UNITY_END();
}
