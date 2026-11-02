/*
 * Effects: parsing a descriptor off the wire, and rendering one.
 *
 * The rendered numbers are the same table `test_curve.c` pins, seen through the
 * endpoints and the step list rather than through the shape alone.
 */
#include <stdio.h>
#include <string.h>

#include "oh_effect.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define CLOSE(want, got, what) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, want, got, what)

static oh_effect_t parsed(oh_datatype_t t, const char *json)
{
    cJSON *body = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL_MESSAGE(body, "the test's own JSON should parse");
    oh_effect_t effect;
    char err[OH_ERR_MAX] = "";
    const bool ok = oh_effect_parse(t, body, &effect, err);
    cJSON_Delete(body);
    TEST_ASSERT_TRUE_MESSAGE(ok, err);
    return effect;
}

static bool rejected(oh_datatype_t t, const char *json, char *err)
{
    cJSON *body = cJSON_Parse(json);
    oh_effect_t effect;
    const bool ok = oh_effect_parse(t, body, &effect, err);
    cJSON_Delete(body);
    return !ok;
}

/* ── Rendering ───────────────────────────────────────────────────────────── */

static void a_one_hertz_sine_peaks_at_250_ms_and_troughs_at_750(void)
{
    oh_effect_t e = parsed(OH_DT_NUMBER,
                           "{\"curve\":{\"shape\":\"sine\"},\"rate\":1.0,\"phase\":0,"
                           "\"low\":{\"value\":0},\"high\":{\"value\":1},\"t0\":0}");
    oh_value_t v;
    TEST_ASSERT_TRUE(oh_effect_sample(&e, 250, &v));
    CLOSE(1.0f, v.value, "peak");
    TEST_ASSERT_TRUE(oh_effect_sample(&e, 750, &v));
    CLOSE(0.0f, v.value, "trough");
    TEST_ASSERT_TRUE(oh_effect_sample(&e, 0, &v));
    CLOSE(0.5f, v.value, "starts halfway");
}

static void two_phases_half_a_cycle_apart_are_mirror_images(void)
{
    oh_effect_t a = parsed(OH_DT_NUMBER, "{\"curve\":{\"shape\":\"sine\"},\"rate\":1.0,\"phase\":0,"
                                         "\"low\":{\"value\":0},\"high\":{\"value\":1},\"t0\":0}");
    oh_effect_t b = parsed(OH_DT_NUMBER, "{\"curve\":{\"shape\":\"sine\"},\"rate\":1.0,\"phase\":0.5,"
                                         "\"low\":{\"value\":0},\"high\":{\"value\":1},\"t0\":0}");
    const int64_t at[] = {0, 125, 250, 375, 500};
    for (size_t i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
        oh_value_t va, vb;
        oh_effect_sample(&a, at[i], &va);
        oh_effect_sample(&b, at[i], &vb);
        CLOSE(1.0f, va.value + vb.value, "two phases half a cycle apart should sum to one");
    }
}

static void low_and_high_scale_the_shape_without_changing_it(void)
{
    oh_effect_t e = parsed(OH_DT_NUMBER, "{\"curve\":{\"shape\":\"sine\"},\"rate\":1.0,\"phase\":0,"
                                         "\"low\":{\"value\":0.2},\"high\":{\"value\":0.6},\"t0\":0}");
    oh_value_t v;
    oh_effect_sample(&e, 250, &v);
    CLOSE(0.6f, v.value, "peaks at high");
    oh_effect_sample(&e, 750, &v);
    CLOSE(0.2f, v.value, "troughs at low");
    oh_effect_sample(&e, 0, &v);
    CLOSE(0.4f, v.value, "halfway between");
}

/* A relay under a square wave is closed for half its cycle. Turning over at the
 * first instant instead would leave it closed for all but a moment. */
static void a_relay_under_a_square_wave_spends_half_the_cycle_closed(void)
{
    oh_effect_t e = parsed(OH_DT_BOOLEAN, "{\"curve\":{\"shape\":\"square\"},\"rate\":1.0,\"phase\":0,"
                                          "\"width\":0.5,\"low\":{\"state\":false},"
                                          "\"high\":{\"state\":true},\"t0\":0}");
    oh_value_t v;
    oh_effect_sample(&e, 0, &v);
    TEST_ASSERT_TRUE(v.state);
    oh_effect_sample(&e, 250, &v);
    TEST_ASSERT_TRUE(v.state);
    oh_effect_sample(&e, 500, &v);
    TEST_ASSERT_FALSE(v.state);
    oh_effect_sample(&e, 750, &v);
    TEST_ASSERT_FALSE(v.state);
}

static void a_color_blends_channel_by_channel(void)
{
    oh_effect_t e = parsed(OH_DT_COLOR, "{\"curve\":{\"shape\":\"sine\"},\"rate\":1.0,\"phase\":0,"
                                        "\"low\":{\"r\":0,\"g\":0,\"b\":0},"
                                        "\"high\":{\"r\":255,\"g\":100,\"b\":0},\"t0\":0}");
    oh_value_t v;
    oh_effect_sample(&e, 250, &v);
    TEST_ASSERT_EQUAL_UINT8(255, v.color.r);
    TEST_ASSERT_EQUAL_UINT8(100, v.color.g);
    TEST_ASSERT_EQUAL_UINT8(0, v.color.b);
    oh_effect_sample(&e, 0, &v);
    TEST_ASSERT_EQUAL_UINT8(128, v.color.r);
    TEST_ASSERT_EQUAL_UINT8(50, v.color.g);
}

/* ── Steps ───────────────────────────────────────────────────────────────── */

#define RGB_CHASE(easing)                                                                                    \
    "{\"curve\":{\"steps\":["                                                                                \
    "{\"at\":0,\"value\":{\"r\":255,\"g\":0,\"b\":0},\"easing\":\"" easing "\"},"                             \
    "{\"at\":0.3333333,\"value\":{\"r\":0,\"g\":255,\"b\":0},\"easing\":\"" easing "\"},"                     \
    "{\"at\":0.6666666,\"value\":{\"r\":0,\"g\":0,\"b\":255},\"easing\":\"" easing "\"}"                      \
    "]},\"rate\":1.0,\"phase\":0,\"t0\":0}"

/* The reason a step carries a value rather than a level: a chase can be three
 * colors, not three brightnesses of one. */
static void a_hard_chase_shows_each_step_from_where_it_starts(void)
{
    oh_effect_t e = parsed(OH_DT_COLOR, RGB_CHASE("step"));
    oh_value_t v;
    oh_effect_sample(&e, 0, &v);
    TEST_ASSERT_EQUAL_UINT8(255, v.color.r);
    oh_effect_sample(&e, 200, &v);
    TEST_ASSERT_EQUAL_UINT8(255, v.color.r);
    oh_effect_sample(&e, 400, &v);
    TEST_ASSERT_EQUAL_UINT8(255, v.color.g);
    oh_effect_sample(&e, 700, &v);
    TEST_ASSERT_EQUAL_UINT8(255, v.color.b);
    oh_effect_sample(&e, 1000, &v);
    TEST_ASSERT_EQUAL_UINT8(255, v.color.r);
}

static void a_linear_chase_crossfades_between_its_steps(void)
{
    oh_effect_t e = parsed(OH_DT_COLOR, RGB_CHASE("linear"));
    oh_value_t v;
    /* A sixth of a cycle in is halfway from the first step to the second. A third
     * of a second is 333.33 ms and the clock counts whole ones, so this lands a
     * couple of counts off the middle, and should. */
    oh_effect_sample(&e, 1000 / 6, &v);
    TEST_ASSERT_INT_WITHIN(4, 128, v.color.r);
    TEST_ASSERT_INT_WITHIN(4, 128, v.color.g);
    TEST_ASSERT_EQUAL_UINT8(0, v.color.b);
}

/* The last step eases into the first, round the end of the cycle, rather than
 * having nowhere to go. */
static void the_last_step_wraps_round_to_the_first(void)
{
    oh_effect_t e = parsed(OH_DT_COLOR, RGB_CHASE("linear"));
    oh_value_t v;
    oh_effect_sample(&e, 5000 / 6, &v);
    TEST_ASSERT_INT_WITHIN(4, 128, v.color.b);
    TEST_ASSERT_INT_WITHIN(4, 128, v.color.r);
    TEST_ASSERT_EQUAL_UINT8(0, v.color.g);
}

/* ── Parsing ─────────────────────────────────────────────────────────────── */

static void a_descriptor_off_the_wire_becomes_something_renderable(void)
{
    oh_effect_t e = parsed(OH_DT_NUMBER,
                           "{\"id\":\"6f0c\",\"curve\":{\"shape\":\"saw-up\"},\"rate\":0.5,"
                           "\"phase\":0.25,\"direction\":\"backward\",\"width\":0.25,"
                           "\"low\":{\"value\":0},\"high\":{\"value\":1},\"t0\":1756550400123}");
    TEST_ASSERT_TRUE(e.active);
    TEST_ASSERT_EQUAL_STRING("6f0c", e.id);
    TEST_ASSERT_EQUAL_INT(OH_SHAPE_SAW_UP, e.shape);
    CLOSE(0.5f, e.rate_hz, "rate");
    CLOSE(0.25f, e.phase01, "phase");
    CLOSE(0.25f, e.width, "width");
    TEST_ASSERT_TRUE(e.backward);
    TEST_ASSERT_EQUAL_INT64(1756550400123LL, e.t0_ms);
}

/* Not an error: this is how a console takes a port back. */
static void a_clear_parses_as_nothing_running(void)
{
    cJSON *body = cJSON_Parse("{\"clear\":true}");
    oh_effect_t e;
    char err[OH_ERR_MAX] = "";
    TEST_ASSERT_TRUE(oh_effect_parse(OH_DT_NUMBER, body, &e, err));
    TEST_ASSERT_FALSE(e.active);
    cJSON_Delete(body);

    oh_value_t v;
    TEST_ASSERT_FALSE_MESSAGE(oh_effect_sample(&e, 500, &v), "and it renders nothing");
}

static void a_descriptor_this_node_cannot_honor_is_refused(void)
{
    char err[OH_ERR_MAX] = "";
    TEST_ASSERT_TRUE(rejected(OH_DT_NUMBER, "{\"rate\":1.0}", err));
    TEST_ASSERT_TRUE(rejected(OH_DT_NUMBER, "{\"curve\":{\"shape\":\"spiral\"},\"rate\":1}", err));
    TEST_ASSERT_TRUE(rejected(OH_DT_NUMBER, "{\"curve\":{\"shape\":\"sine\"}}", err));
    TEST_ASSERT_TRUE(rejected(OH_DT_NUMBER, "{\"curve\":{\"shape\":\"sine\"},\"rate\":-1}", err));
    /* A step's value goes through the same parser a plain `set` uses, so a
     * boolean payload on a number port is refused on exactly the same terms. */
    TEST_ASSERT_TRUE(rejected(OH_DT_NUMBER,
                              "{\"curve\":{\"steps\":[{\"at\":0,\"value\":{\"state\":true}}]},\"rate\":1}",
                              err));
    TEST_ASSERT_TRUE(rejected(OH_DT_NUMBER, "{\"curve\":{\"steps\":[]},\"rate\":1}", err));
}

/*
 * Seventeen keyframes is one more than this node can hold.
 *
 * Refused rather than truncated: a chase silently missing its last step is a fault
 * an operator would chase for an hour, and the console can be told the limit.
 */
static void a_step_list_longer_than_the_node_can_hold_is_refused(void)
{
    char json[2048] = "{\"curve\":{\"steps\":[";
    for (int i = 0; i < OH_EFFECT_STEPS_MAX + 1; i++) {
        char one[64];
        snprintf(one, sizeof(one), "%s{\"at\":%.3f,\"value\":{\"value\":1}}", i ? "," : "",
                 (double)i / (OH_EFFECT_STEPS_MAX + 1));
        strncat(json, one, sizeof(json) - strlen(json) - 1);
    }
    strncat(json, "]},\"rate\":1}", sizeof(json) - strlen(json) - 1);

    char err[OH_ERR_MAX] = "";
    TEST_ASSERT_TRUE(rejected(OH_DT_NUMBER, json, err));
    TEST_ASSERT_NOT_NULL(strstr(err, "hold"));
}

/* A shape with no endpoints still renders, between this port's own nothing and
 * everything, which is plainly what a console that omitted them meant. */
static void a_shape_with_no_endpoints_uses_the_ports_own_range(void)
{
    oh_effect_t e = parsed(OH_DT_NUMBER, "{\"curve\":{\"shape\":\"saw-up\"},\"rate\":1,\"t0\":0}");
    oh_value_t v;
    oh_effect_sample(&e, 500, &v);
    CLOSE(0.5f, v.value, "half way up");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_one_hertz_sine_peaks_at_250_ms_and_troughs_at_750);
    RUN_TEST(two_phases_half_a_cycle_apart_are_mirror_images);
    RUN_TEST(low_and_high_scale_the_shape_without_changing_it);
    RUN_TEST(a_relay_under_a_square_wave_spends_half_the_cycle_closed);
    RUN_TEST(a_color_blends_channel_by_channel);
    RUN_TEST(a_hard_chase_shows_each_step_from_where_it_starts);
    RUN_TEST(a_linear_chase_crossfades_between_its_steps);
    RUN_TEST(the_last_step_wraps_round_to_the_first);
    RUN_TEST(a_descriptor_off_the_wire_becomes_something_renderable);
    RUN_TEST(a_clear_parses_as_nothing_running);
    RUN_TEST(a_descriptor_this_node_cannot_honor_is_refused);
    RUN_TEST(a_step_list_longer_than_the_node_can_hold_is_refused);
    RUN_TEST(a_shape_with_no_endpoints_uses_the_ports_own_range);
    return UNITY_END();
}
