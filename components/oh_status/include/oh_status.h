/* The status LED: what the node is doing, at a glance, and the identify blink. */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oh_status_start(void);

#ifdef __cplusplus
}
#endif
