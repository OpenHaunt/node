/*
 * The mix, against a clock the test holds: a clip starts on the frame its show time
 * falls on, a late one joins where it would be by now, a loop and a scare overlap,
 * a stop fades, and the stream follows its mapping through loss and drift.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "oh_mixer.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* A source over an array, counting releases. */
typedef struct {
    const int16_t *s;
    uint32_t frames;
    uint8_t channels;
    uint32_t at;
    int released;
} arr_t;

static size_t arr_pull(void *ctx, int16_t *out, size_t n)
{
    arr_t *a = ctx;
    if (a->at >= a->frames) {
        return 0;
    }
    if (n > a->frames - a->at) {
        n = a->frames - a->at;
    }
    memcpy(out, a->s + (size_t)a->at * a->channels, n * a->channels * sizeof(int16_t));
    a->at += (uint32_t)n;
    return n;
}

static bool arr_seek(void *ctx, uint32_t f)
{
    arr_t *a = ctx;
    a->at = f > a->frames ? a->frames : f;
    return true;
}

static void arr_release(void *ctx)
{
    ((arr_t *)ctx)->released++;
}

static oh_mix_source_t source(arr_t *a, uint32_t rate)
{
    oh_mix_source_t s = {arr_pull, arr_seek, arr_release, a, a->channels, rate, a->frames};
    return s;
}

static oh_mixer_t mixer;
static int16_t out[48000 * 2];

/* A clip whose samples count up from 1, so a frame says where it came from. */
static int16_t counting[48000];

static void fill_counting(void)
{
    for (int i = 0; i < 48000; i++) {
        counting[i] = (int16_t)(i % 30000 + 1);
    }
}

#define T0 1000000000LL /* show time of the first rendered frame, in microseconds */

static void a_clip_starts_on_the_frame_its_show_time_falls_on(void)
{
    fill_counting();
    oh_mixer_init(&mixer);
    arr_t a = {counting, 4800, 1, 0, 0};
    oh_mix_source_t src = source(&a, OH_MIX_RATE);
    /* 2.5 ms in: frame 120 of the block. */
    TEST_ASSERT_TRUE(oh_mixer_play(&mixer, "scare", "c", &src, T0 + 2500, 0, false, 1.0f, 0));
    oh_mixer_render(&mixer, T0, out, 480);
    TEST_ASSERT_EQUAL_INT16(0, out[2 * 119]);
    TEST_ASSERT_EQUAL_INT16(1, out[2 * 120]);
    TEST_ASSERT_EQUAL_INT16(1, out[2 * 120 + 1]); /* mono on both channels */
    TEST_ASSERT_EQUAL_INT16(360, out[2 * 480 - 2]);
    /* It plays out and ends by itself, releasing its source once. */
    for (int k = 0; k < 12; k++) {
        oh_mixer_render(&mixer, T0 + 10000 * (k + 1), out, 480);
    }
    TEST_ASSERT_FALSE(oh_mixer_busy(&mixer));
    TEST_ASSERT_EQUAL_INT(1, a.released);
}

static void a_late_command_joins_where_the_clip_would_be_by_now(void)
{
    fill_counting();
    oh_mixer_init(&mixer);
    arr_t a = {counting, 48000, 1, 0, 0};
    oh_mix_source_t src = source(&a, OH_MIX_RATE);
    /* Due 100 ms before the block: it should be 4800 frames in. The first 10 ms ramp
     * in, so look past them. */
    oh_mixer_play(&mixer, "jaw", "c", &src, T0 - 100000, 0, false, 1.0f, 0);
    oh_mixer_render(&mixer, T0, out, 960);
    TEST_ASSERT_EQUAL_INT16(4801 + 900, out[2 * 900]);

    /* A late clip whose end has already passed plays nothing at all. */
    oh_mixer_init(&mixer);
    arr_t b = {counting, 480, 1, 0, 0};
    src = source(&b, OH_MIX_RATE);
    oh_mixer_play(&mixer, "jaw", "c", &src, T0 - 100000, 0, false, 1.0f, 0);
    oh_mixer_render(&mixer, T0, out, 480);
    TEST_ASSERT_FALSE(oh_mixer_busy(&mixer));
    TEST_ASSERT_EQUAL_INT(1, b.released);
}

static int16_t ones[480];
static int16_t tens[48000];

static void a_loop_and_a_scare_overlap_and_the_loop_has_no_gap(void)
{
    for (int i = 0; i < 480; i++) {
        ones[i] = 1;
    }
    for (int i = 0; i < 48000; i++) {
        tens[i] = 10;
    }
    oh_mixer_init(&mixer);
    arr_t amb = {ones, 480, 1, 0, 0};
    arr_t scare = {tens, 4800, 1, 0, 0};
    oh_mix_source_t s1 = source(&amb, OH_MIX_RATE), s2 = source(&scare, OH_MIX_RATE);
    oh_mixer_play(&mixer, "ambient", "a", &s1, T0, 0, true, 1.0f, 0);
    oh_mixer_play(&mixer, "scare", "b", &s2, T0 + 20000, 0, false, 1.0f, 0);
    oh_mixer_render(&mixer, T0, out, 4800);
    /* 10 ms of a 480-frame loop is many laps: never a zero in it. */
    for (int i = 0; i < 960; i++) {
        TEST_ASSERT_EQUAL_INT16(1, out[2 * i]);
    }
    TEST_ASSERT_EQUAL_INT16(11, out[2 * 2000]);
    TEST_ASSERT_TRUE(oh_mixer_uses_clip(&mixer, "a"));
}

static void a_stop_fades_and_a_replacement_crossfades(void)
{
    for (int i = 0; i < 48000; i++) {
        tens[i] = 1000;
    }
    oh_mixer_init(&mixer);
    arr_t a = {tens, 48000, 1, 0, 0};
    oh_mix_source_t s = source(&a, OH_MIX_RATE);
    oh_mixer_play(&mixer, "v", "a", &s, T0, 0, false, 1.0f, 0);
    /* Stop 5 ms in over 10 ms: frame 240 starts the ramp, 720 is silent. */
    oh_mixer_stop(&mixer, "v", T0 + 5000, 10);
    oh_mixer_render(&mixer, T0, out, 960);
    TEST_ASSERT_EQUAL_INT16(1000, out[2 * 239]);
    TEST_ASSERT_TRUE(out[2 * 480] > 400 && out[2 * 480] < 600);
    TEST_ASSERT_EQUAL_INT16(0, out[2 * 800]);
    TEST_ASSERT_EQUAL_INT(1, a.released);

    /* Playing on a busy name: the old one fades out under the new one. */
    oh_mixer_init(&mixer);
    arr_t b = {tens, 48000, 1, 0, 0}, c = {tens, 48000, 1, 0, 0};
    s = source(&b, OH_MIX_RATE);
    oh_mixer_play(&mixer, "v", "a", &s, T0, 0, false, 1.0f, 0);
    oh_mixer_render(&mixer, T0, out, 480);
    s = source(&c, OH_MIX_RATE);
    oh_mixer_play(&mixer, "v", "b", &s, T0 + 10000, 0, false, 0.5f, 0);
    oh_mixer_render(&mixer, T0 + 10000, out, 960);
    /* Halfway through the crossfade: half of the old plus all of the new. */
    TEST_ASSERT_TRUE(out[2 * 240] > 900 && out[2 * 240] < 1100);
    TEST_ASSERT_EQUAL_INT16(500, out[2 * 900]);
    TEST_ASSERT_EQUAL_INT(1, b.released);
}

static void a_fifth_voice_steals_the_oldest(void)
{
    for (int i = 0; i < 48000; i++) {
        tens[i] = 10;
    }
    oh_mixer_init(&mixer);
    arr_t a[5];
    const char *names[5] = {"a", "b", "c", "d", "e"};
    for (int i = 0; i < 5; i++) {
        a[i] = (arr_t){tens, 48000, 1, 0, 0};
        oh_mix_source_t s = source(&a[i], OH_MIX_RATE);
        TEST_ASSERT_TRUE(oh_mixer_play(&mixer, names[i], "x", &s, T0, 0, false, 1.0f, 0));
    }
    /* "a" never started, so it went at once and nothing sounded. */
    TEST_ASSERT_EQUAL_INT(1, a[0].released);
    oh_mixer_render(&mixer, T0, out, 480);
    TEST_ASSERT_EQUAL_INT16(40, out[2 * 100]);
}

static void a_clip_at_another_rate_is_resampled(void)
{
    static int16_t slow[24000];
    for (int i = 0; i < 24000; i++) {
        slow[i] = (int16_t)(i * 2);
    }
    oh_mixer_init(&mixer);
    arr_t a = {slow, 24000, 1, 0, 0};
    oh_mix_source_t s = source(&a, 24000);
    oh_mixer_play(&mixer, "v", "a", &s, T0, 0, false, 1.0f, 0);
    oh_mixer_render(&mixer, T0, out, 2000);
    /* Twice as many output frames as source frames, the odd ones half way between. */
    TEST_ASSERT_EQUAL_INT16(1000, out[2 * 1000]);
    TEST_ASSERT_EQUAL_INT16(1002, out[2 * 1002]);
    TEST_ASSERT_EQUAL_INT16(1001, out[2 * 1001]);
}

static void four_loud_voices_clip_rather_than_wrap(void)
{
    static int16_t loud[4800];
    for (int i = 0; i < 4800; i++) {
        loud[i] = 30000;
    }
    oh_mixer_init(&mixer);
    arr_t a[2] = {{loud, 4800, 1, 0, 0}, {loud, 4800, 1, 0, 0}};
    for (int i = 0; i < 2; i++) {
        oh_mix_source_t s = source(&a[i], OH_MIX_RATE);
        oh_mixer_play(&mixer, i ? "x" : "y", "a", &s, T0, 0, false, 1.0f, 0);
    }
    oh_mixer_render(&mixer, T0, out, 480);
    TEST_ASSERT_EQUAL_INT16(32767, out[2 * 100]);
    /* The master gain scales the whole mix, after the sum. */
    oh_mixer_gain(&mixer, NULL, 0.5f, 0);
    oh_mixer_render(&mixer, T0 + 10000, out, 960);
    TEST_ASSERT_EQUAL_INT16(30000, out[2 * 900]);
}

/* ── The stream ── */

#define CAP 48000
static int16_t jframes[CAP];
static uint32_t jtags[CAP / OH_JITTER_BLOCK];
static uint32_t jvalid[CAP / 32];

static void feed(oh_jitter_t *j, uint32_t ts, size_t n, int16_t (*f)(uint32_t))
{
    uint8_t p[2 * 960];
    for (size_t i = 0; i < n; i++) {
        const int16_t v = f(ts + (uint32_t)i);
        p[2 * i] = (uint8_t)((uint16_t)v >> 8);
        p[2 * i + 1] = (uint8_t)v;
    }
    oh_jitter_write_l16be(j, oh_jitter_widen(j, ts), p, n);
}

static int16_t by_ts(uint32_t ts)
{
    return (int16_t)(ts % 20000);
}

static void the_stream_plays_each_sample_at_its_mapped_show_time(void)
{
    oh_jitter_t j;
    oh_jitter_init(&j, jframes, jtags, jvalid, CAP, 1);
    oh_jitter_reset(&j, 5000);
    for (uint32_t ts = 5000; ts < 5000 + 9600; ts += 240) {
        feed(&j, ts, 240, by_ts);
    }
    oh_mixer_init(&mixer);
    /* ts 5000 is heard at T0 + 10 ms. */
    oh_mixer_stream_start(&mixer, &j, 5004, 48000, 5000, T0 + 10000, 1.0f);
    oh_mixer_render(&mixer, T0, out, 4800);
    TEST_ASSERT_EQUAL_INT16(0, out[2 * 479]);
    /* After the 10 ms fade-in, frame i is ts 5000 + (i - 480). */
    TEST_ASSERT_EQUAL_INT16(by_ts(5000 + 2000), out[2 * (480 + 2000)]);
    TEST_ASSERT_EQUAL_INT16(by_ts(5000 + 4000), out[2 * (480 + 4000)]);
}

static int16_t loud_ts(uint32_t ts)
{
    (void)ts;
    return 10000;
}

static void a_lost_packet_fades_out_instead_of_clicking_and_does_not_shift_the_rest(void)
{
    oh_jitter_t j;
    oh_jitter_init(&j, jframes, jtags, jvalid, CAP, 1);
    oh_jitter_reset(&j, 0);
    for (uint32_t ts = 0; ts < 9600; ts += 240) {
        if (ts != 2400 && ts != 2640 && ts != 2880 && ts != 3120) { /* 20 ms lost */
            feed(&j, ts, 240, loud_ts);
        }
    }
    oh_mixer_init(&mixer);
    oh_mixer_stream_start(&mixer, &j, 5004, 48000, 0, T0, 1.0f);
    oh_mixer_render(&mixer, T0, out, 6000);
    TEST_ASSERT_EQUAL_INT16(10000, out[2 * 2399]);
    /* Into the gap, decaying, never a step to zero. */
    TEST_ASSERT_TRUE(out[2 * 2410] < 10000 && out[2 * 2410] > 9000);
    TEST_ASSERT_TRUE(out[2 * 3300] < 200);
    /* And back exactly on time after it. */
    TEST_ASSERT_EQUAL_INT16(10000, out[2 * 3400]);
    TEST_ASSERT_EQUAL_UINT32(960, mixer.stream.missing);
}

static void drift_is_trimmed_away_and_a_jump_is_resynced(void)
{
    oh_jitter_t j;
    oh_jitter_init(&j, jframes, jtags, jvalid, CAP, 1);
    oh_jitter_reset(&j, 0);
    for (uint32_t ts = 0; ts < 40000; ts += 240) {
        feed(&j, ts, 240, by_ts);
    }
    oh_mixer_init(&mixer);
    oh_mixer_stream_start(&mixer, &j, 5004, 48000, 0, T0, 1.0f);
    oh_mixer_render(&mixer, T0, out, 480);
    /* The console re-anchors 5 ms later than before: a small move is trimmed... */
    oh_mixer_stream_start(&mixer, &j, 5004, 48000, 0, T0 + 5000, 1.0f);
    int64_t t = T0 + 10000;
    oh_mixer_render(&mixer, t, out, 480);
    TEST_ASSERT_EQUAL_UINT32(0, mixer.stream.resyncs);
    TEST_ASSERT_TRUE(mixer.stream.trim < 0);
    for (int k = 0; k < 600; k++) {
        t += 10000;
        oh_mixer_render(&mixer, t, out, 480);
    }
    const double expected = (double)mixer.stream.anchor_ext + (double)(t + 10000 - (T0 + 5000)) * 48.0 / 1000.0;
    TEST_ASSERT_TRUE(fabs(mixer.stream.pos - expected) < 48.0); /* within 1 ms */
    /* ...and a big one is a jump. */
    oh_mixer_stream_start(&mixer, &j, 5004, 48000, 0, T0 + 105000, 1.0f);
    oh_mixer_render(&mixer, t + 10000, out, 480);
    TEST_ASSERT_EQUAL_UINT32(1, mixer.stream.resyncs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(a_clip_starts_on_the_frame_its_show_time_falls_on);
    RUN_TEST(a_late_command_joins_where_the_clip_would_be_by_now);
    RUN_TEST(a_loop_and_a_scare_overlap_and_the_loop_has_no_gap);
    RUN_TEST(a_stop_fades_and_a_replacement_crossfades);
    RUN_TEST(a_fifth_voice_steals_the_oldest);
    RUN_TEST(a_clip_at_another_rate_is_resampled);
    RUN_TEST(four_loud_voices_clip_rather_than_wrap);
    RUN_TEST(the_stream_plays_each_sample_at_its_mapped_show_time);
    RUN_TEST(a_lost_packet_fades_out_instead_of_clicking_and_does_not_shift_the_rest);
    RUN_TEST(drift_is_trimmed_away_and_a_jump_is_resynced);
    return UNITY_END();
}
