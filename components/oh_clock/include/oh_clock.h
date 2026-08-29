/*
 * The node's clock. Timestamps on the wire are unix milliseconds once SNTP
 * has set the time, and milliseconds since boot before that. Inputs are
 * stamped with esp_timer microseconds in the ISR and converted here, so an
 * edge that happened before the clock synced is still placed correctly
 * relative to now.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t oh_clock_now_ms(void);
bool oh_clock_is_unix(void);
/* Convert an esp_timer_get_time() reading to the wire clock. */
int64_t oh_clock_from_us(int64_t esp_timer_us);
uint32_t oh_clock_uptime_s(void);

#ifdef __cplusplus
}
#endif
