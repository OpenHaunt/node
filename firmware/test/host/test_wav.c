/*
 * A stored clip, read back: the header, both formats, seeking, and what is refused.
 */
#include "audio_fixtures.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static int16_t ramp(uint32_t i, uint8_t c)
{
    return (int16_t)((int32_t)(i * 7) - 20000 + c * 100);
}

static int16_t sine(uint32_t i, uint8_t c)
{
    return (int16_t)(12000.0 * sin((double)i * 2.0 * M_PI * 440.0 / 32000.0 + c));
}

static oh_wav_reader_t reader;

static void open_clip(const uint8_t *w, size_t len, mem_io_t *io)
{
    oh_wav_info_t info;
    char err[OH_ERR_MAX] = "";
    TEST_ASSERT_TRUE_MESSAGE(oh_wav_parse(w, len, (uint32_t)len, &info, err), err);
    io->buf = w;
    io->len = len;
    io->at = 0;
    oh_io_t o = {mem_read, mem_seek, io};
    oh_wav_reader_init(&reader, &info, &o);
    TEST_ASSERT_TRUE(oh_wav_reader_seek(&reader, 0));
}

static void pcm16_reads_back_exactly_past_a_list_chunk(void)
{
    size_t len;
    uint8_t *w = wav_pcm16(48000, 2, 5000, ramp, &len);
    oh_wav_info_t info;
    TEST_ASSERT_TRUE(oh_wav_parse(w, len, (uint32_t)len, &info, NULL));
    TEST_ASSERT_EQUAL_UINT16(OH_WAV_PCM16, info.format);
    TEST_ASSERT_EQUAL_UINT8(2, info.channels);
    TEST_ASSERT_EQUAL_UINT32(5000, info.frames);
    TEST_ASSERT_EQUAL_UINT32(104, oh_wav_duration_ms(&info));

    mem_io_t io;
    open_clip(w, len, &io);
    static int16_t out[6000 * 2];
    size_t got = 0, n;
    while ((n = oh_wav_reader_read(&reader, out + got * 2, 700)) > 0) {
        got += n;
    }
    TEST_ASSERT_EQUAL_size_t(5000, got);
    for (uint32_t i = 0; i < 5000; i++) {
        TEST_ASSERT_EQUAL_INT16(ramp(i, 0), out[i * 2]);
        TEST_ASSERT_EQUAL_INT16(ramp(i, 1), out[i * 2 + 1]);
    }
    /* Seeking lands on the frame asked for. */
    TEST_ASSERT_TRUE(oh_wav_reader_seek(&reader, 4321));
    TEST_ASSERT_EQUAL_size_t(1, oh_wav_reader_read(&reader, out, 1));
    TEST_ASSERT_EQUAL_INT16(ramp(4321, 0), out[0]);
    free(w);
}

static void ima_adpcm_decodes_close_to_the_source_mono_and_stereo(void)
{
    for (uint8_t ch = 1; ch <= 2; ch++) {
        size_t len;
        const uint16_t align = ch == 1 ? 1024 : 2048;
        uint8_t *w = wav_ima(32000, ch, 20000, align, sine, &len);
        oh_wav_info_t info;
        char err[OH_ERR_MAX] = "";
        TEST_ASSERT_TRUE_MESSAGE(oh_wav_parse(w, len, (uint32_t)len, &info, err), err);
        TEST_ASSERT_EQUAL_UINT16(OH_WAV_IMA_ADPCM, info.format);
        TEST_ASSERT_EQUAL_UINT16(ch == 1 ? 2041 : 2041, info.frames_per_block);
        /* A short last block holds the remainder, rounded up to a group of eight. */
        TEST_ASSERT_TRUE(info.frames >= 20000 && info.frames < 20008);

        mem_io_t io;
        open_clip(w, len, &io);
        static int16_t out[21000 * 2];
        size_t got = 0, n;
        while ((n = oh_wav_reader_read(&reader, out + got * ch, 333)) > 0) {
            got += n;
        }
        TEST_ASSERT_EQUAL_size_t(info.frames, got);
        double err2 = 0;
        for (uint32_t i = 0; i < 20000; i++) {
            for (uint8_t c = 0; c < ch; c++) {
                const double d = out[i * ch + c] - sine(i, c);
                err2 += d * d;
            }
        }
        /* 4-bit ADPCM of a 12000-peak sine comes back about 34 dB above its error
         * (172 RMS here). A decoder out of step with the encoder is thousands. */
        const double rms = sqrt(err2 / (20000.0 * ch));
        TEST_ASSERT_TRUE(rms < 250);
        /* Every block starts exactly on its header's sample. */
        TEST_ASSERT_EQUAL_INT16(sine(info.frames_per_block, 0), out[info.frames_per_block * ch]);
        free(w);
    }
}

static void seeking_into_an_adpcm_block_matches_reading_up_to_it(void)
{
    size_t len;
    uint8_t *w = wav_ima(32000, 1, 9000, 512, sine, &len);
    mem_io_t io;
    open_clip(w, len, &io);
    static int16_t all[9100];
    size_t got = 0, n;
    while ((n = oh_wav_reader_read(&reader, all + got, 1000)) > 0) {
        got += n;
    }
    for (uint32_t at = 0; at < 9000; at += 997) {
        int16_t one[4];
        TEST_ASSERT_TRUE(oh_wav_reader_seek(&reader, at));
        TEST_ASSERT_EQUAL_size_t(4, oh_wav_reader_read(&reader, one, 4));
        TEST_ASSERT_EQUAL_INT16_ARRAY(all + at, one, 4);
    }
    /* Past the end is the end, not an error. */
    TEST_ASSERT_TRUE(oh_wav_reader_seek(&reader, 99999));
    TEST_ASSERT_EQUAL_size_t(0, oh_wav_reader_read(&reader, all, 10));
    free(w);
}

static void what_this_node_cannot_play_is_refused_with_a_reason(void)
{
    char err[OH_ERR_MAX];
    oh_wav_info_t info;
    uint8_t w[128];

    TEST_ASSERT_FALSE(oh_wav_parse((const uint8_t *)"ID3\x04 this is an mp3", 20, 20, &info, err));
    TEST_ASSERT_EQUAL_STRING("not a RIFF WAVE file", err);

    size_t h = wav_header(w, OH_WAV_PCM16, 1, 48000, 3, 24, 30, false);
    TEST_ASSERT_FALSE(oh_wav_parse(w, h + 30, (uint32_t)h + 30, &info, err));
    TEST_ASSERT_EQUAL_STRING("PCM must be 16-bit", err);

    h = wav_header(w, 3 /* float */, 1, 48000, 4, 32, 40, false);
    TEST_ASSERT_FALSE(oh_wav_parse(w, h + 40, (uint32_t)h + 40, &info, err));
    TEST_ASSERT_EQUAL_STRING("only PCM16 or IMA ADPCM", err);

    h = wav_header(w, OH_WAV_PCM16, 6, 48000, 12, 16, 40, false);
    TEST_ASSERT_FALSE(oh_wav_parse(w, h + 40, (uint32_t)h + 40, &info, err));
    TEST_ASSERT_EQUAL_STRING("only mono or stereo", err);

    /* A truncated upload is not a clip. */
    h = wav_header(w, OH_WAV_PCM16, 1, 48000, 2, 16, 4000, false);
    TEST_ASSERT_FALSE(oh_wav_parse(w, h, (uint32_t)h + 100, &info, err));
    TEST_ASSERT_EQUAL_STRING("data chunk runs past the end of the file", err);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(pcm16_reads_back_exactly_past_a_list_chunk);
    RUN_TEST(ima_adpcm_decodes_close_to_the_source_mono_and_stereo);
    RUN_TEST(seeking_into_an_adpcm_block_matches_reading_up_to_it);
    RUN_TEST(what_this_node_cannot_play_is_refused_with_a_reason);
    return UNITY_END();
}
