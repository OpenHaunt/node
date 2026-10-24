/*
 * Audio on the wire: the commands a console sends, what is refused, and the shapes
 * the node answers with.
 */
#include <string.h>

#include "oh_audio_proto.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define SHA "3f9a0000000000000000000000000000000000000000000000000000000000ab"

static bool parse(const char *verb, const char *json, oh_audio_cmd_t *c, char *err)
{
    cJSON *j = cJSON_Parse(json);
    const bool ok = oh_audio_cmd_parse(verb, j, c, err);
    cJSON_Delete(j);
    return ok;
}

static void play_carries_a_voice_a_clip_and_a_show_time(void)
{
    oh_audio_cmd_t c;
    char err[OH_ERR_MAX] = "";
    TEST_ASSERT_TRUE_MESSAGE(parse("play",
                                   "{\"voice\":\"ambient\",\"clip\":\"" SHA "\",\"at\":1761830400123,"
                                   "\"loop\":true,\"gain\":0.6,\"fade_in_ms\":2000,\"offset_ms\":250}",
                                   &c, err),
                             err);
    TEST_ASSERT_EQUAL_INT(OH_AUDIO_PLAY, c.verb);
    TEST_ASSERT_EQUAL_STRING("ambient", c.voice);
    TEST_ASSERT_EQUAL_STRING(SHA, c.clip);
    TEST_ASSERT_TRUE(c.has_at);
    TEST_ASSERT_EQUAL_INT64(1761830400123LL, c.at_ms);
    TEST_ASSERT_TRUE(c.loop);
    TEST_ASSERT_EQUAL_FLOAT(0.6f, c.gain);
    TEST_ASSERT_EQUAL_UINT32(2000, c.fade_in_ms);
    TEST_ASSERT_EQUAL_UINT32(250, c.offset_ms);

    /* With nothing optional: now, once, at full gain. */
    TEST_ASSERT_TRUE(parse("play", "{\"voice\":\"s\",\"clip\":\"" SHA "\"}", &c, err));
    TEST_ASSERT_FALSE(c.has_at);
    TEST_ASSERT_FALSE(c.loop);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.gain);
}

static void what_a_node_cannot_act_on_is_refused(void)
{
    oh_audio_cmd_t c;
    char err[OH_ERR_MAX];
    TEST_ASSERT_FALSE(parse("play", "{\"clip\":\"" SHA "\"}", &c, err));
    TEST_ASSERT_EQUAL_STRING("voice is required", err);
    TEST_ASSERT_FALSE(parse("play", "{\"voice\":\"s\",\"clip\":\"scream.wav\"}", &c, err));
    TEST_ASSERT_EQUAL_STRING("clip must be a lowercase sha256", err);
    TEST_ASSERT_FALSE(parse("play", "{\"voice\":\"stream\",\"clip\":\"" SHA "\"}", &c, err));
    TEST_ASSERT_FALSE(parse("play", "{\"voice\":\"s\",\"clip\":\"" SHA "\",\"gain\":1.5}", &c, err));
    TEST_ASSERT_EQUAL_STRING("gain must be 0..1", err);
    TEST_ASSERT_FALSE(parse("play", "{\"voice\":\"a-name-much-too-long\",\"clip\":\"" SHA "\"}", &c, err));
    TEST_ASSERT_FALSE(parse("gain", "{\"voice\":\"s\"}", &c, err));
    TEST_ASSERT_EQUAL_STRING("gain is required", err);
    TEST_ASSERT_FALSE(parse("output", "{\"output\":\"speaker\"}", &c, err));
    TEST_ASSERT_FALSE(parse("stream", "{\"op\":\"start\",\"port\":5004,\"rate\":48000,\"ts\":0}", &c, err));
    TEST_ASSERT_EQUAL_STRING("at is required for a stream", err);
    TEST_ASSERT_FALSE(parse("stream", "{\"op\":\"start\",\"port\":80,\"rate\":48000,\"ts\":0,\"at\":1}", &c, err));
    TEST_ASSERT_FALSE(parse("whisper", "{}", &c, err));
}

static void stop_gain_output_and_stream_parse(void)
{
    oh_audio_cmd_t c;
    char err[OH_ERR_MAX] = "";
    TEST_ASSERT_TRUE(parse("stop", "{}", &c, err));
    TEST_ASSERT_EQUAL_INT(OH_AUDIO_STOP, c.verb);
    TEST_ASSERT_EQUAL_STRING("", c.voice); /* everything */
    TEST_ASSERT_TRUE(parse("stop", "{\"voice\":\"scare\",\"at\":5,\"fade_ms\":500}", &c, err));
    TEST_ASSERT_EQUAL_UINT32(500, c.fade_ms);
    TEST_ASSERT_TRUE(parse("gain", "{\"gain\":0.3,\"fade_ms\":1500}", &c, err));
    TEST_ASSERT_EQUAL_STRING("", c.voice); /* master */
    TEST_ASSERT_TRUE(parse("output", "{\"output\":\"speaker\",\"enabled\":false}", &c, err));
    TEST_ASSERT_FALSE(c.enabled);
    TEST_ASSERT_TRUE_MESSAGE(parse("stream",
                                   "{\"op\":\"start\",\"port\":5004,\"rate\":44100,\"channels\":2,"
                                   "\"ssrc\":305419896,\"ts\":4294967000,\"at\":1761830400123}",
                                   &c, err),
                             err);
    TEST_ASSERT_EQUAL_INT(OH_AUDIO_STREAM_START, c.verb);
    TEST_ASSERT_EQUAL_UINT16(5004, c.port);
    TEST_ASSERT_EQUAL_UINT32(44100, c.rate);
    TEST_ASSERT_EQUAL_UINT8(2, c.channels);
    TEST_ASSERT_TRUE(c.has_ssrc);
    TEST_ASSERT_EQUAL_UINT32(305419896u, c.ssrc);
    TEST_ASSERT_EQUAL_UINT32(4294967000u, c.ts);
    TEST_ASSERT_TRUE(parse("stream", "{\"op\":\"stop\",\"fade_ms\":200}", &c, err));
    TEST_ASSERT_EQUAL_INT(OH_AUDIO_STREAM_STOP, c.verb);
}

static void info_advertises_outputs_formats_and_the_budget(void)
{
    oh_audio_caps_t caps = {.rate = 48000, .voices = 4, .n_outputs = 2, .bytes_total = 10264576,
                            .bytes_free = 9871360, .max_buffer_ms = 1000};
    caps.outputs[0] = (oh_audio_output_t){"line", "Line out", "line", 2};
    caps.outputs[1] = (oh_audio_output_t){"speaker", "Speaker", "amp", 1};
    cJSON *info = cJSON_CreateObject();
    oh_json_audio_caps(info, &caps);
    char *s = cJSON_PrintUnformatted(info);
    TEST_ASSERT_EQUAL_STRING(
        "{\"audio\":{\"rate\":48000,\"voices\":4,\"outputs\":["
        "{\"id\":\"line\",\"name\":\"Line out\",\"kind\":\"line\",\"channels\":2},"
        "{\"id\":\"speaker\",\"name\":\"Speaker\",\"kind\":\"amp\",\"channels\":1}],"
        "\"clips\":{\"container\":\"wav\",\"formats\":[\"pcm16\",\"ima-adpcm\"],"
        "\"bytes_total\":10264576,\"bytes_free\":9871360},"
        "\"stream\":{\"payload\":\"L16\",\"max_buffer_ms\":1000}}}",
        s);
    cJSON_free(s);
    cJSON_Delete(info);
}

static void status_lists_named_voices_and_the_stream(void)
{
    oh_mixer_t m;
    oh_mixer_init(&m);
    m.slots[0].active = true;
    m.slots[0].started = true;
    strcpy(m.slots[0].name, "ambient");
    strcpy(m.slots[0].clip, SHA);
    m.slots[0].src.rate = 32000;
    m.slots[0].pos = 64000;
    m.slots[0].loop = true;
    m.slots[0].level = 0.5f;
    /* A voice fading out after it was replaced is nobody's and is not listed. */
    m.slots[1].active = true;
    m.slots[1].ending = true;
    oh_audio_counters_t c = {.underruns = 0, .locked = true};
    cJSON *st = oh_json_audio_status(&m, &c);
    char *s = cJSON_PrintUnformatted(st);
    TEST_ASSERT_EQUAL_STRING("{\"voices\":[{\"voice\":\"ambient\",\"clip\":\"" SHA "\",\"pos_ms\":2000,"
                             "\"loop\":true,\"started\":true,\"gain\":0.5}],"
                             "\"stream\":{\"active\":false},\"underruns\":0,\"master\":1,\"locked\":true}",
                             s);
    cJSON_free(s);
    cJSON_Delete(st);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(play_carries_a_voice_a_clip_and_a_show_time);
    RUN_TEST(what_a_node_cannot_act_on_is_refused);
    RUN_TEST(stop_gain_output_and_stream_parse);
    RUN_TEST(info_advertises_outputs_formats_and_the_budget);
    RUN_TEST(status_lists_named_voices_and_the_stream);
    return UNITY_END();
}
