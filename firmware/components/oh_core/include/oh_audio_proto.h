/*
 * Audio on the wire: the `audio` object in /api/v1/info, the commands on
 * openhaunt/<sn>/audio/<verb>, and the status message. OpenHaunt/node doc 13.
 *
 * No ESP-IDF dependency: compiled into the host tests unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "oh_mixer.h"
#include "oh_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OH_AUDIO_MAX_OUTPUTS 2
#define OH_AUDIO_OUTPUT_ID_MAX 12

typedef struct {
    char id[OH_AUDIO_OUTPUT_ID_MAX];
    char name[OH_NAME_MAX];
    const char *kind; /* "line" | "amp" */
    uint8_t channels;
} oh_audio_output_t;

typedef struct {
    uint32_t rate;
    uint8_t voices;
    oh_audio_output_t outputs[OH_AUDIO_MAX_OUTPUTS];
    uint8_t n_outputs;
    uint32_t bytes_total;
    uint32_t bytes_free;
    uint32_t max_buffer_ms;
} oh_audio_caps_t;

/* Add `"audio": {...}` to an /api/v1/info object. */
void oh_json_audio_caps(cJSON *info, const oh_audio_caps_t *caps);

typedef enum {
    OH_AUDIO_PLAY = 0,
    OH_AUDIO_STOP,
    OH_AUDIO_GAIN,
    OH_AUDIO_OUTPUT,
    OH_AUDIO_STREAM_START,
    OH_AUDIO_STREAM_STOP,
} oh_audio_verb_t;

typedef struct {
    oh_audio_verb_t verb;
    char voice[OH_VOICE_NAME_MAX]; /* "" = all (stop) or master (gain) */
    char clip[OH_CLIP_ID_MAX];
    bool has_at;
    int64_t at_ms;
    bool loop;
    float gain;
    uint32_t fade_ms;
    uint32_t fade_in_ms;
    uint32_t offset_ms;
    char output[OH_AUDIO_OUTPUT_ID_MAX];
    bool enabled;
    uint16_t port;
    uint32_t rate;
    uint8_t channels;
    bool has_ssrc;
    uint32_t ssrc;
    uint32_t ts;
} oh_audio_cmd_t;

/*
 * Parse one message on openhaunt/<sn>/audio/<verb>. `verb` is the trailing topic
 * segment: play, stop, gain, output or stream. Rejects a missing or malformed field
 * with a reason in `err`.
 */
bool oh_audio_cmd_parse(const char *verb, const cJSON *json, oh_audio_cmd_t *out, char err[OH_ERR_MAX]);

/* A clip id is the lowercase hex sha256 of the file: 64 characters. */
bool oh_audio_clip_id_valid(const char *id);

typedef struct {
    uint32_t underruns;
    bool locked;
    uint32_t stream_late;
    uint32_t stream_depth_frames;
} oh_audio_counters_t;

/* openhaunt/<sn>/audio/status. */
cJSON *oh_json_audio_status(const oh_mixer_t *m, const oh_audio_counters_t *c);

#ifdef __cplusplus
}
#endif
