/* The serial REPL: `oh help` lists what it can do. */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oh_console_start(void);

#ifdef __cplusplus
}
#endif
