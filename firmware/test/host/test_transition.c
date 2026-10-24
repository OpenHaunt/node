/*
 * A move to somewhere, over a stated time, starting at a stated moment.
 *
 * The whole reason this exists is a message count: a three second fade at 40 Hz was
 * a hundred and twenty publishes to a node that could have been told once.
 */
#include "oh_effect.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define CLOSE(want, got, what) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, want, got, what)

static oh_value_t number(float v)
{
    oh_value_t out = {.type = OH_DT_NUMBER};
    out.value = v;
    return out;
}

static bool parse(const char *json, const oh_value_t *from, const oh_value_t *to, int64_t now,
                  oh_transition_t *out)
{
    cJSON *body = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(body);
    const bool timed = oh_transition_parse(body, from, to, now, out);
    cJSON_Delete(body);
    return timed;
}

/*
 * The fast path, and the reason all of this is additive rather than a new protocol.
 *
 * A `set` with none of the timing keys is what every node has always received, and
 * it must not become a zero-length transition on the way through: that would put an
 * ordinary relay command on the rendering path forty times a second.
 */
static void a_plain_set_carries_no_timing(void)
{
    oh_transition_t t;
    const oh_value_t from = number(0.0f), to = number(1.0f);
    TEST_ASSERT_FALSE(parse("{\"value\":1.0}", &from, &to, 0, &t));
    TEST_ASSERT_FALSE(parse("{\"state\":true}", &from, &to, 0, &t));
}

static void a_timed_set_reaches_its_destination_at_delay_plus_fade(void)
{
    oh_transition_t t;
    const oh_value_t from = number(0.0f), to = number(1.0f);
    TEST_ASSERT_TRUE(parse("{\"value\":1.0,\"fade_ms\":3000,\"delay_ms\":500,"
                           "\"curve\":\"linear\",\"t0\":1000}",
                           &from, &to, 0, &t));
    TEST_ASSERT_EQUAL_INT64(1500, t.t0_ms);
    TEST_ASSERT_EQUAL_UINT32(3000, t.duration_ms);
    TEST_ASSERT_EQUAL_INT(OH_EASE_LINEAR, t.easing);

    oh_value_t v;
    bool done = true;
    /* Inside its delay: held where it started. */
    TEST_ASSERT_TRUE(oh_transition_sample(&t, 1200, &v, &done));
    CLOSE(0.0f, v.value, "waiting");
    TEST_ASSERT_FALSE(done);

    TEST_ASSERT_TRUE(oh_transition_sample(&t, 3000, &v, &done));
    CLOSE(0.5f, v.value, "half way");
    TEST_ASSERT_FALSE(done);

    TEST_ASSERT_TRUE(oh_transition_sample(&t, 4500, &v, &done));
    CLOSE(1.0f, v.value, "there");
    TEST_ASSERT_TRUE(done);
}

/* A node with no `t0` of its own to work from starts the move now, which is what a
 * console that omitted it plainly meant. */
static void a_set_with_timing_but_no_anchor_starts_now(void)
{
    oh_transition_t t;
    const oh_value_t from = number(0.0f), to = number(1.0f);
    TEST_ASSERT_TRUE(parse("{\"value\":1.0,\"fade_ms\":1000}", &from, &to, 5000, &t));
    TEST_ASSERT_EQUAL_INT64(5000, t.t0_ms);
}

static void an_easing_shapes_the_move(void)
{
    oh_transition_t t;
    const oh_value_t from = number(0.0f), to = number(1.0f);
    parse("{\"value\":1.0,\"fade_ms\":1000,\"curve\":\"ease-in\",\"t0\":0}", &from, &to, 0, &t);

    oh_value_t v;
    bool done;
    oh_transition_sample(&t, 500, &v, &done);
    TEST_ASSERT_TRUE_MESSAGE(v.value < 0.5f, "ease-in is slow off the mark");
    CLOSE(0.25f, v.value, "and quadratically so");
}

/* A move with no time to make it in arrives at once, which is the same thing an
 * untimed `set` does — just reached by a different route. */
static void a_zero_length_fade_is_simply_arrival(void)
{
    oh_transition_t t;
    const oh_value_t from = number(0.0f), to = number(1.0f);
    parse("{\"value\":1.0,\"fade_ms\":0,\"t0\":0}", &from, &to, 0, &t);

    oh_value_t v;
    bool done = false;
    TEST_ASSERT_TRUE(oh_transition_sample(&t, 1, &v, &done));
    CLOSE(1.0f, v.value, "there at once");
    TEST_ASSERT_TRUE(done);
}

/* A color walks channel by channel, so a fade from red to blue passes through
 * purple rather than through black. */
static void a_color_fade_walks_every_channel(void)
{
    oh_transition_t t;
    oh_value_t from = {.type = OH_DT_COLOR}, to = {.type = OH_DT_COLOR};
    from.color = (oh_color_t){255, 0, 0};
    to.color = (oh_color_t){0, 0, 255};
    parse("{\"r\":0,\"g\":0,\"b\":255,\"fade_ms\":1000,\"t0\":0}", &from, &to, 0, &t);

    oh_value_t v;
    bool done;
    oh_transition_sample(&t, 500, &v, &done);
    TEST_ASSERT_INT_WITHIN(2, 128, v.color.r);
    TEST_ASSERT_INT_WITHIN(2, 128, v.color.b);
    TEST_ASSERT_EQUAL_UINT8(0, v.color.g);
}

static void nothing_running_samples_as_nothing(void)
{
    oh_transition_t t = {0};
    oh_value_t v;
    bool done;
    TEST_ASSERT_FALSE(oh_transition_sample(&t, 500, &v, &done));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_plain_set_carries_no_timing);
    RUN_TEST(a_timed_set_reaches_its_destination_at_delay_plus_fade);
    RUN_TEST(a_set_with_timing_but_no_anchor_starts_now);
    RUN_TEST(an_easing_shapes_the_move);
    RUN_TEST(a_zero_length_fade_is_simply_arrival);
    RUN_TEST(a_color_fade_walks_every_channel);
    RUN_TEST(nothing_running_samples_as_nothing);
    return UNITY_END();
}
