/*
 * The status display on boards that have one: what the node is, where it is
 * on the network, which module it is being and what its ports hold. Text
 * only, one screen, redrawn when something changes.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oh_display_start(void);
/* 0..3: quarter turns. Persisted; takes effect immediately. */
esp_err_t oh_display_set_rotation(int rotation);
int oh_display_rotation(void);

#ifdef __cplusplus
}
#endif
