/* What the clip store and the audio task tell each other. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "oh_mixer.h"

#define OH_CLIPS_BASE "/clips"

esp_err_t oh_clips_mount(void);
bool oh_clips_mounted(void);
void oh_clips_budget(uint32_t *total, uint32_t *free_bytes);

/* A mixer source over a stored clip, or false if it is not stored or not playable.
 * The source's release() closes the file and frees the reader. */
bool oh_clips_open(const char *sha, oh_mix_source_t *src);

/* Whether a voice is playing `sha` right now (takes the mixer lock). */
bool oh_audio_clip_in_use(const char *sha);
