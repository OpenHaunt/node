/*
 * A node that makes sound: clips stored on its flash and an RTP stream, mixed and
 * played at a stated show time. OpenHaunt/node doc 13 is the protocol; the pure
 * parts (the WAV reader, the mixer, the jitter buffer, the wire shapes) are in
 * oh_core and host-tested. This is the I2S driver, the clip store on FAT, the UDP
 * receiver and the task that ties them to the clock.
 *
 * On a board without audio every call is a harmless no-op, so the HTTP and MQTT
 * layers do not have to know which board they are on.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"
#include "oh_audio_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mount the clip store, bring up I2S, start the mixer. ESP_OK and nothing else on a
 * board without audio. */
esp_err_t oh_audio_start(void);
bool oh_audio_available(void);
void oh_audio_caps(oh_audio_caps_t *out);

/* One message on openhaunt/<sn>/audio/<verb>. */
void oh_audio_command(const char *verb, const char *data, size_t len);

/* What openhaunt/<sn>/audio/status should say now, or NULL when there is nothing to
 * publish: once a second while anything plays, and once more when it goes quiet. */
cJSON *oh_audio_status_poll(void);

/* ── The clip store, for /api/v1/audio/clips ── */

/* Begin an upload of `len` bytes that must hash to `sha`. Answers the HTTP status to
 * send if the upload should not go ahead (200 already stored, 400, 409, 413, 503), or
 * 0 to go ahead; `msg` says why. */
int oh_clips_upload_begin(const char *sha, size_t len, const char **msg);
esp_err_t oh_clips_upload_write(const void *buf, size_t n);
/* 201 on success; 400 when the bytes do not hash to the sha or are not a playable WAV. */
int oh_clips_upload_finish(const char **msg);
void oh_clips_upload_abort(void);

/* {"clips":[...],"bytes_total":..,"bytes_free":..} */
cJSON *oh_clips_list(void);
/* 204, 404, or 409 while a voice is playing it. */
int oh_clips_delete(const char *sha);

#ifdef __cplusplus
}
#endif
