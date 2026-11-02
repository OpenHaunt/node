/*
 * Firmware update over HTTP into the other app slot, with the bootloader's
 * rollback: a new image that does not get as far as the network is booted
 * over on the next reset.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oh_ota_session oh_ota_session_t;

esp_err_t oh_ota_begin(size_t total_size, oh_ota_session_t **out);
esp_err_t oh_ota_write(oh_ota_session_t *s, const void *data, size_t len);
/* Validate and make the new image the one to boot. `version` receives its
 * app version. The caller reboots. */
esp_err_t oh_ota_end(oh_ota_session_t *s, char *version, size_t version_cap);
void oh_ota_abort(oh_ota_session_t *s);

/* True while this image has booted but not yet been confirmed. */
bool oh_ota_pending_verify(void);
/* Confirm this image so the bootloader stops considering a rollback. */
esp_err_t oh_ota_confirm(void);
/* Register: confirm automatically once the network is up. */
esp_err_t oh_ota_start(void);
const char *oh_ota_running_partition(void);

#ifdef __cplusplus
}
#endif
