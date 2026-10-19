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

/*
 * The console's clock.
 *
 * An effect's phase is a position on a clock every node on the broker shares, and
 * this node's own is not that clock. The console publishes `openhaunt/clock` once a
 * second, retained, and `oh_mqtt` feeds every message here.
 *
 * With no sample yet, `oh_clock_now_ms()` is the answer — which is right when SNTP
 * has synced, because the console is on the same LAN and assumed NTP-close, and no
 * worse than any other guess when it has not. A shape rendered against a guess is
 * in the wrong place; a shape not rendered at all is a light that never comes on.
 */
/* Where the console says what time it is. One topic for every node on the broker,
 * deliberately not under a serial: they all have to agree with each other. */
#define OH_CLOCK_TOPIC "openhaunt/clock"

void oh_clock_console_feed(int64_t console_ms, uint64_t seq, bool retained);
int64_t oh_clock_console_now_ms(void);
int64_t oh_clock_console_offset_ms(void);
bool oh_clock_console_locked(void);

/*
 * The console's clock in microseconds, for audio, which places a sample to within a
 * fraction of a millisecond. Same estimate as oh_clock_console_now_ms(); the extra
 * digits come from this node's own clock rather than from a finer estimate.
 */
int64_t oh_clock_console_now_us(void);

#ifdef __cplusplus
}
#endif
