#include <math.h>
#include <string.h>

#include "oh_pixfx.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define N 60
static const int64_t T = 1760000000000LL; /* a console clock in 2025, in ms */

static oh_pixfx_params_t params(oh_pixfx_kind_t kind)
{
    oh_pixfx_params_t p;
    oh_pixfx_defaults(&p);
    p.kind = kind;
    p.colour = (oh_color_t){255, 0, 0};
    p.colour2 = (oh_color_t){0, 0, 255};
    p.brightness = 1.0f;
    return p;
}

static void solid_is_the_old_strip(void)
{
    oh_pixfx_params_t p = params(OH_PIXFX_SOLID);
    p.colour = (oh_color_t){200, 100, 50};
    p.brightness = 0.5f;
    uint8_t rgb[3 * N];
    oh_pixfx_render(&p, 0.3f, T, N, rgb);
    for (int i = 0; i < N; i++) {
        TEST_ASSERT_EQUAL(100, rgb[3 * i]);
        TEST_ASSERT_EQUAL(50, rgb[3 * i + 1]);
        TEST_ASSERT_EQUAL(25, rgb[3 * i + 2]);
    }
}

static void a_chase_lights_size_of_the_segment_and_moves_with_the_phase(void)
{
    oh_pixfx_params_t p = params(OH_PIXFX_CHASE);
    p.size = 0.25f;
    uint8_t a[3 * N], b[3 * N];
    oh_pixfx_render(&p, 0.0f, T, N, a);
    int lit = 0;
    for (int i = 0; i < N; i++) {
        lit += a[3 * i] == 255;
    }
    TEST_ASSERT_EQUAL(15, lit);
    TEST_ASSERT_EQUAL(255, a[0]);
    oh_pixfx_render(&p, 0.5f, T, N, b);
    TEST_ASSERT_EQUAL(0, b[0]);
    TEST_ASSERT_EQUAL(255, b[3 * 30]);
}

static void segments_repeat_and_mirror_and_reverse_fold(void)
{
    oh_pixfx_params_t p = params(OH_PIXFX_GRADIENT);
    p.segments = 2;
    uint8_t rgb[3 * N];
    oh_pixfx_render(&p, 0.0f, T, N, rgb);
    TEST_ASSERT_EQUAL(rgb[0], rgb[3 * 30]);       /* two segments, the same pattern */
    TEST_ASSERT_TRUE(rgb[0] > rgb[3 * 29]);        /* Colour at the start, Colour 2 at the end */
    p.segments = 1;
    p.mirror = true;
    oh_pixfx_render(&p, 0.0f, T, N, rgb);
    TEST_ASSERT_EQUAL(rgb[0], rgb[3 * (N - 1)]);   /* folded about the middle */
    p.mirror = false;
    p.reverse = true;
    oh_pixfx_render(&p, 0.0f, T, N, rgb);
    TEST_ASSERT_TRUE(rgb[0] < rgb[3 * (N - 1)]);
}

static void the_random_effects_are_the_same_on_every_node(void)
{
    for (int k = OH_PIXFX_FIRE; k <= OH_PIXFX_LIGHTNING; k++) {
        oh_pixfx_params_t p = params((oh_pixfx_kind_t)k);
        p.size = 0.6f;
        p.speed = 3.0f;
        uint8_t a[3 * N], b[3 * N], c[3 * N];
        oh_pixfx_render(&p, 0.1f, T + 1234, N, a);
        oh_pixfx_render(&p, 0.9f, T + 1234, N, b); /* the phase is not what drives them */
        TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a));
        /* ...and they do move. */
        bool moved = false;
        for (int64_t dt = 50; dt < 3000 && !moved; dt += 50) {
            oh_pixfx_render(&p, 0.1f, T + 1234 + dt, N, c);
            moved = memcmp(a, c, sizeof(a)) != 0;
        }
        TEST_ASSERT_TRUE_MESSAGE(moved, oh_pixfx_str((oh_pixfx_kind_t)k));
    }
}

static void lightning_strikes_sometimes_and_is_dark_otherwise(void)
{
    oh_pixfx_params_t p = params(OH_PIXFX_LIGHTNING);
    p.colour = (oh_color_t){255, 255, 255};
    p.colour2 = (oh_color_t){0, 0, 0};
    p.speed = 0.5f;
    p.size = 0.5f;
    int lit = 0, frames = 0;
    uint8_t rgb[3 * N];
    for (int64_t t = 0; t < 600000; t += 20, frames++) {
        oh_pixfx_render(&p, 0.0f, T + t, N, rgb);
        lit += rgb[0] == 255;
    }
    /* ~150 windows, half of them striking, ~2.5 flashes of ~60 ms: a few percent lit. */
    TEST_ASSERT_TRUE(lit > frames / 100);
    TEST_ASSERT_TRUE(lit < frames / 10);
}

static void the_phase_is_exact_at_a_trillion_milliseconds(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, oh_pixfx_phase_at(1.0f, 1760000000500LL));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, oh_pixfx_phase_at(0.5f, 1760000000500LL));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, oh_pixfx_phase_at(0.0f, 1760000000500LL));
}

static void two_strips_started_apart_lock_together(void)
{
    oh_pixfx_clock_t a = {0}, b = {0};
    oh_pixfx_advance(&a, 1.0f, T);
    /* b runs at another speed for a while, then joins a. */
    for (int64_t t = 0; t < 2000; t += 25) {
        oh_pixfx_advance(&b, 2.7f, T + t);
    }
    for (int64_t t = 0; t < 10000; t += 25) {
        oh_pixfx_advance(&a, 1.0f, T + 2000 + t);
        oh_pixfx_advance(&b, 1.0f, T + 2000 + t);
    }
    float d = fabsf(a.phase - b.phase);
    if (d > 0.5f) {
        d = 1.0f - d;
    }
    TEST_ASSERT_TRUE(d < 0.01f);
}

static void a_speed_fade_never_makes_the_phase_jump(void)
{
    oh_pixfx_clock_t c = {0};
    float last = -1;
    for (int64_t t = 0; t < 4000; t += 25) {
        const float speed = 0.5f + (float)t / 1000.0f; /* 0.5 to 4.5 Hz */
        oh_pixfx_advance(&c, speed, T + t);
        if (last >= 0) {
            float step = c.phase - last;
            if (step < -0.5f) {
                step += 1.0f;
            }
            TEST_ASSERT_TRUE(step >= 0.0f);
            TEST_ASSERT_TRUE(step <= speed * 0.025f + 0.001f);
        }
        last = c.phase;
    }
}

static void names_round_trip_and_an_unknown_one_is_solid(void)
{
    for (int k = 0; k < OH_PIXFX_COUNT; k++) {
        oh_pixfx_kind_t back;
        TEST_ASSERT_TRUE(oh_pixfx_parse(oh_pixfx_str((oh_pixfx_kind_t)k), &back));
        TEST_ASSERT_EQUAL(k, back);
    }
    oh_pixfx_kind_t back;
    TEST_ASSERT_FALSE(oh_pixfx_parse("plasma", &back));
    TEST_ASSERT_EQUAL(OH_PIXFX_SOLID, back);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(solid_is_the_old_strip);
    RUN_TEST(a_chase_lights_size_of_the_segment_and_moves_with_the_phase);
    RUN_TEST(segments_repeat_and_mirror_and_reverse_fold);
    RUN_TEST(the_random_effects_are_the_same_on_every_node);
    RUN_TEST(lightning_strikes_sometimes_and_is_dark_otherwise);
    RUN_TEST(the_phase_is_exact_at_a_trillion_milliseconds);
    RUN_TEST(two_strips_started_apart_lock_together);
    RUN_TEST(a_speed_fade_never_makes_the_phase_jump);
    RUN_TEST(names_round_trip_and_an_unknown_one_is_solid);
    return UNITY_END();
}
