/* The status LED: what the node is doing, at a glance, and the identify blink. */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oh_status_start(void);

/* Whether the board has a BOOT button, and if so whether it is held right now. A press
 * toggles dark mode (oh_config_set_dark). */
bool oh_status_button(bool *pressed);

#ifdef __cplusplus
}
#endif
