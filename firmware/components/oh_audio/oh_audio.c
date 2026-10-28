#include "oh_audio.h"
#include "oh_audio_priv.h"

#include <inttypes.h>
#include <string.h>

#include "sdkconfig.h"

#if CONFIG_OH_AUDIO

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "oh_amp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "oh_board.h"
#include "oh_clock.h"
#include "oh_node.h"

static const char *TAG = "oh_audio";

/* One output block: 10 ms. The DMA ring is DMA_DESC of them, so about 60 ms of sound
 * is queued ahead of the DAC: enough to ride out a flash sector erase, and latency
 * that costs nothing, because every sample is placed by its show time. */
#define BLOCK 480
#define DMA_DESC 6
/* The stream's buffer: a second at 48 kHz. */
#define JB_FRAMES 48000
#define MAX_BUFFER_MS ((JB_FRAMES * 1000) / OH_MIX_RATE)
/* Power the speaker amplifier this far ahead of a sound, and drop it this long after one. */
#define AMP_LEAD_US 300000
#define AMP_IDLE_US 2000000

static struct {
    bool up;
    i2s_chan_handle_t tx;
    SemaphoreHandle_t lock;
    oh_mixer_t *mix;
    oh_jitter_t jb;
    int16_t *jframes;
    uint32_t *jtags;
    uint32_t *jvalid;
    /* Written by the I2S ISR: frames that have left for the DAC, and when. */
    volatile uint64_t sent;
    volatile int64_t sent_at_us;
    uint64_t written;
    int64_t next_block_show_us;
    uint32_t underruns;
    bool downmix;
    bool line_enabled;
    bool speaker_enabled;
    oh_amp_t amp;
    /* The stream's receiver. */
    uint16_t want_port;
    bool has_ssrc;
    uint32_t ssrc;
    /* What reached the receiver this session; see oh_audio_counters_t. */
    oh_rtp_seq_t seq;
    uint32_t foreign;
    int64_t last_rx_us;
    uint32_t rx_gap_ms;
    uint32_t rx_gap_max_ms;
    bool was_busy;
    const oh_board_pins_t *pins;
} s;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool IRAM_ATTR on_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *ctx)
{
    (void)handle;
    (void)event;
    (void)ctx;
    portENTER_CRITICAL_ISR(&s_mux);
    s.sent += BLOCK;
    s.sent_at_us = esp_timer_get_time();
    portEXIT_CRITICAL_ISR(&s_mux);
    return false;
}

static void set_pin(int gpio, bool level)
{
    if (gpio >= 0) {
        gpio_set_level(gpio, level ? 1 : 0);
    }
}

static void apply_outputs(void)
{
    set_pin(s.pins->dac_mute, s.line_enabled);
    set_pin(s.pins->amp_en, s.speaker_enabled && s.amp.on);
}

/* ── The output task ─────────────────────────────────────────────────── */

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t buf[BLOCK * 2];
    for (;;) {
        uint64_t sent;
        int64_t sent_at;
        portENTER_CRITICAL(&s_mux);
        sent = s.sent;
        sent_at = s.sent_at_us;
        portEXIT_CRITICAL(&s_mux);
        if (s.written <= sent) {
            /* The DMA ran dry and played silence: the next block we write follows the
             * buffer it is in the middle of. */
            if (s.written != 0) {
                s.underruns++;
            }
            s.written = sent + BLOCK;
        }
        /* Frame `sent` began playing at `sent_at`; ours is (written - sent) frames later. */
        const int64_t local_us = sent_at + (int64_t)(((s.written - sent) * 1000000u) / OH_MIX_RATE);
        const int64_t offset_us = oh_clock_console_now_us() - esp_timer_get_time();
        const int64_t show_us = local_us + offset_us;

        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.next_block_show_us = show_us + (BLOCK * 1000000LL) / OH_MIX_RATE;
        oh_mixer_render(s.mix, show_us, buf, BLOCK);
        const bool wanted = oh_mixer_sounding(s.mix) || oh_mixer_next_start_us(s.mix) - show_us < AMP_LEAD_US;
        xSemaphoreGive(s.lock);

        if (s.downmix) {
            /* V0.1's only output is an amplifier wired to play one slot. */
            for (int i = 0; i < BLOCK; i++) {
                const int16_t m = (int16_t)(((int32_t)buf[2 * i] + buf[2 * i + 1]) / 2);
                buf[2 * i] = buf[2 * i + 1] = m;
            }
        }

        /* On just ahead of a sound and off a while after — or, where the board's
         * output hums when let go of, on for good (oh_amp.h). */
        if (oh_amp_step(&s.amp, wanted, show_us)) {
            apply_outputs();
        }

        size_t w = 0;
        i2s_channel_write(s.tx, buf, sizeof(buf), &w, portMAX_DELAY);
        s.written += BLOCK;
    }
}

/* ── The stream's receiver ───────────────────────────────────────────── */

static void rx_task(void *arg)
{
    (void)arg;
    static uint8_t pkt[1500];
    int sock = -1;
    uint16_t bound = 0;
    for (;;) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        if (!s.mix->stream.active) {
            s.want_port = 0;
        }
        const uint16_t want = s.want_port;
        xSemaphoreGive(s.lock);
        if (want != bound) {
            if (sock >= 0) {
                close(sock);
                sock = -1;
            }
            bound = 0;
            if (want) {
                sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(want),
                                           .sin_addr.s_addr = htonl(INADDR_ANY)};
                struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
                int rcvbuf = 32 * 1024;
                setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
                if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
                    ESP_LOGE(TAG, "stream: cannot listen on udp/%u", want);
                    if (sock >= 0) {
                        close(sock);
                        sock = -1;
                    }
                    vTaskDelay(pdMS_TO_TICKS(500));
                    continue;
                }
                bound = want;
                ESP_LOGI(TAG, "stream: listening on udp/%u", want);
            }
        }
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        const int n = recv(sock, pkt, sizeof(pkt), 0);
        if (n <= 0) {
            continue;
        }
        oh_rtp_packet_t r;
        if (!oh_rtp_parse(pkt, (size_t)n, &r)) {
            continue;
        }
        /* Stamped before the lock: time spent waiting for it is the task not reading. */
        const int64_t now_us = esp_timer_get_time();
        xSemaphoreTake(s.lock, portMAX_DELAY);
        if (!s.has_ssrc || r.ssrc == s.ssrc) {
            const size_t frames = r.payload_len / (2u * s.jb.channels);
            oh_jitter_write_l16be(&s.jb, oh_jitter_widen(&s.jb, r.ts), r.payload, frames);
            const uint32_t gap_ms = s.last_rx_us ? (uint32_t)((now_us - s.last_rx_us) / 1000) : 0;
            if (s.last_rx_us) {
                if (gap_ms > s.rx_gap_ms) {
                    s.rx_gap_ms = gap_ms;
                }
                if (gap_ms > s.rx_gap_max_ms) {
                    s.rx_gap_max_ms = gap_ms;
                }
            }
            s.last_rx_us = now_us;
            const uint32_t skipped = oh_rtp_seq_note(&s.seq, r.seq);
            if (skipped) {
                /* The wait that overflowed the queue is before the packets queued behind
                 * it, not on the one after the gap: so the longest wait this second. */
                ESP_LOGW(TAG, "stream: %" PRIu32 " packet(s) before seq %u never arrived; longest wait "
                              "for a packet this second %" PRIu32 " ms",
                         skipped, r.seq, s.rx_gap_ms);
            }
        } else {
            s.foreign++;
        }
        xSemaphoreGive(s.lock);
    }
}

/* ── Commands ────────────────────────────────────────────────────────── */

void oh_audio_command(const char *verb, const char *data, size_t len)
{
    if (!s.up) {
        ESP_LOGW(TAG, "audio/%s: this node has no audio", verb);
        return;
    }
    cJSON *json = cJSON_ParseWithLength(data, len);
    oh_audio_cmd_t c;
    char err[OH_ERR_MAX] = "";
    const bool ok = oh_audio_cmd_parse(verb, json, &c, err);
    cJSON_Delete(json);
    if (!ok) {
        ESP_LOGW(TAG, "audio/%s: %s", verb, err);
        return;
    }
    /* Open the file before taking the lock: the output task must not wait on FAT. */
    oh_mix_source_t src;
    if (c.verb == OH_AUDIO_PLAY && !oh_clips_open(c.clip, &src)) {
        ESP_LOGW(TAG, "play %s: clip %.12s… is not stored", c.voice, c.clip);
        return;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    /* No `at` means the next block the output task writes. */
    const int64_t at_us = c.has_at ? c.at_ms * 1000 : s.next_block_show_us;
    switch (c.verb) {
    case OH_AUDIO_PLAY:
        oh_mixer_play(s.mix, c.voice, c.clip, &src, at_us, c.offset_ms, c.loop, c.gain, c.fade_in_ms);
        break;
    case OH_AUDIO_STOP:
    case OH_AUDIO_STREAM_STOP:
        oh_mixer_stop(s.mix, c.voice, at_us, c.fade_ms);
        break;
    case OH_AUDIO_GAIN:
        if (!oh_mixer_gain(s.mix, c.voice, c.gain, c.fade_ms)) {
            ESP_LOGW(TAG, "gain: no voice %s", c.voice);
        }
        break;
    case OH_AUDIO_OUTPUT:
        if (strcmp(c.output, "line") == 0 && s.pins->dac_mute >= 0) {
            s.line_enabled = c.enabled;
        } else if (strcmp(c.output, "speaker") == 0 && s.pins->amp_en >= 0) {
            s.speaker_enabled = c.enabled;
        } else {
            ESP_LOGW(TAG, "output: no output %s", c.output);
        }
        apply_outputs();
        break;
    case OH_AUDIO_STREAM_START: {
        /* The same session re-anchored keeps its buffer; a new one, and a console
         * that restarted is one under a new ssrc, starts from an empty buffer. */
        const bool reanchor =
            oh_mixer_stream_continues(s.mix, c.port, c.rate, c.channels, c.has_ssrc, c.ssrc);
        if (!reanchor) {
            oh_jitter_init(&s.jb, s.jframes, s.jtags, s.jvalid, JB_FRAMES, c.channels);
            oh_jitter_reset(&s.jb, c.ts);
            oh_rtp_seq_reset(&s.seq);
            s.foreign = 0;
            s.last_rx_us = 0;
            s.rx_gap_ms = 0;
            s.rx_gap_max_ms = 0;
        }
        s.has_ssrc = c.has_ssrc;
        s.ssrc = c.ssrc;
        s.want_port = c.port;
        oh_mixer_stream_start(s.mix, &s.jb, c.port, c.rate, c.ts, at_us, c.gain, c.has_ssrc, c.ssrc);
        break;
    }
    }
    xSemaphoreGive(s.lock);
}

bool oh_audio_clip_in_use(const char *sha)
{
    if (!s.up) {
        return false;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const bool used = oh_mixer_uses_clip(s.mix, sha);
    xSemaphoreGive(s.lock);
    return used;
}

cJSON *oh_audio_status_poll(void)
{
    if (!s.up) {
        return NULL;
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const bool busy = oh_mixer_busy(s.mix);
    const bool publish = busy || s.was_busy;
    s.was_busy = busy;
    cJSON *j = NULL;
    if (publish) {
        const oh_audio_counters_t c = {
            .underruns = s.underruns,
            .locked = oh_clock_console_locked(),
            .stream_late = s.jb.late,
            .stream_depth_frames = oh_jitter_depth(&s.jb),
            .stream_packets = s.seq.packets,
            .stream_seq_lost = s.seq.lost,
            .stream_reordered = s.seq.reordered,
            .stream_duplicates = s.seq.duplicates,
            .stream_foreign = s.foreign,
            .stream_rx_gap_ms = s.rx_gap_ms,
            .stream_rx_gap_max_ms = s.rx_gap_max_ms,
        };
        j = oh_json_audio_status(s.mix, &c);
        s.rx_gap_ms = 0;
    }
    xSemaphoreGive(s.lock);
    return j;
}

/* ── Start ───────────────────────────────────────────────────────────── */

bool oh_audio_available(void)
{
    return s.up;
}

void oh_audio_caps(oh_audio_caps_t *out)
{
    memset(out, 0, sizeof(*out));
    out->rate = OH_MIX_RATE;
    out->voices = OH_MIX_VOICES;
    out->max_buffer_ms = MAX_BUFFER_MS;
    if (s.pins && s.pins->dac_mute >= 0) {
        out->outputs[out->n_outputs++] = (oh_audio_output_t){"line", "Line out", "line", 2};
    }
    if (s.pins && s.pins->amp_en >= 0) {
        out->outputs[out->n_outputs++] = (oh_audio_output_t){"speaker", "Speaker", "amp", 1};
    }
    oh_clips_budget(&out->bytes_total, &out->bytes_free);
}

static void *psram(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
    return p ? p : heap_caps_calloc(1, n, MALLOC_CAP_DEFAULT);
}

esp_err_t oh_audio_start(void)
{
    s.pins = oh_board_pins();
    if (s.pins->i2s_dout < 0 || s.pins->i2s_bclk < 0 || s.pins->i2s_ws < 0) {
        return ESP_OK;
    }
    s.lock = xSemaphoreCreateMutex();
    s.mix = psram(sizeof(oh_mixer_t));
    s.jframes = psram(JB_FRAMES * 2 * sizeof(int16_t));
    s.jtags = psram((JB_FRAMES / OH_JITTER_BLOCK) * sizeof(uint32_t));
    s.jvalid = psram(((JB_FRAMES + 31) / 32) * sizeof(uint32_t));
    if (!s.lock || !s.mix || !s.jframes || !s.jtags || !s.jvalid) {
        ESP_LOGE(TAG, "out of memory");
        oh_node_error_set("audio");
        return ESP_ERR_NO_MEM;
    }
    oh_mixer_init(s.mix);
    oh_jitter_init(&s.jb, s.jframes, s.jtags, s.jvalid, JB_FRAMES, 1);

    if (oh_clips_mount() != ESP_OK) {
        /* The stream still works without a store. */
        oh_node_error_set("clip-store");
    }

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = DMA_DESC;
    chan.dma_frame_num = BLOCK;
    chan.auto_clear_after_cb = true;
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(OH_MIX_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = s.pins->i2s_bclk,
                .ws = s.pins->i2s_ws,
                .dout = s.pins->i2s_dout,
                .din = I2S_GPIO_UNUSED,
            },
    };
    esp_err_t err = i2s_new_channel(&chan, &s.tx, NULL);
    if (err == ESP_OK) {
        err = i2s_channel_init_std_mode(s.tx, &std);
    }
    const i2s_event_callbacks_t cbs = {.on_sent = on_sent};
    if (err == ESP_OK) {
        err = i2s_channel_register_event_callback(s.tx, &cbs, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s: %s", esp_err_to_name(err));
        oh_node_error_set("audio");
        return err;
    }
    /* Fill the ring with silence so the first frame we write has a known place. */
    static const int16_t zeros[BLOCK * 2];
    size_t loaded = 0, total = 0;
    do {
        loaded = 0;
        i2s_channel_preload_data(s.tx, zeros, sizeof(zeros), &loaded);
        total += loaded;
    } while (loaded == sizeof(zeros));
    s.written = total / (2 * sizeof(int16_t));
    s.sent = 0;
    s.sent_at_us = esp_timer_get_time();
    i2s_channel_enable(s.tx);

    /* V0.1 has the amplifier alone, wired to one slot; V0.2's amplifier mixes (L+R)/2
     * itself and has a stereo line out beside it. */
    s.downmix = s.pins->dac_mute < 0;
    s.line_enabled = true;
    s.speaker_enabled = true;
#ifdef CONFIG_OH_AUDIO_AMP_ALWAYS_ON
    oh_amp_init(&s.amp, AMP_IDLE_US, true);
#else
    oh_amp_init(&s.amp, AMP_IDLE_US, false);
#endif
    apply_outputs();

    s.up = true;
    xTaskCreatePinnedToCore(audio_task, "oh_audio", 4096, NULL, 19, NULL, 1);
    xTaskCreatePinnedToCore(rx_task, "oh_audio_rx", 4096, NULL, 12, NULL, 0);
    ESP_LOGI(TAG, "%u Hz, %u voices, %s", OH_MIX_RATE, OH_MIX_VOICES,
             s.pins->dac_mute >= 0 ? "line out + speaker" : "speaker");
    return ESP_OK;
}

#else /* !CONFIG_OH_AUDIO */

esp_err_t oh_audio_start(void)
{
    return ESP_OK;
}

bool oh_audio_available(void)
{
    return false;
}

void oh_audio_caps(oh_audio_caps_t *out)
{
    memset(out, 0, sizeof(*out));
}

void oh_audio_command(const char *verb, const char *data, size_t len)
{
    (void)verb;
    (void)data;
    (void)len;
}

cJSON *oh_audio_status_poll(void)
{
    return NULL;
}

#endif
