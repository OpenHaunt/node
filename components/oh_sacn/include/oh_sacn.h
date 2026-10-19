/*
 * E1.31 (sACN) reception: UDP 5568, unicast and each wanted universe's multicast
 * group. Two kinds of consumer share the one socket — a gateway forwarding the
 * universe its config names, and every strip in `sacn` mode (doc 14), each
 * subscribing to the universes it reads.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "oh_e131.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*oh_sacn_sink_t)(void *ctx, const oh_e131_frame_t *frame);

esp_err_t oh_sacn_start(void);

/*
 * Hand every frame of `universe` to `sink`, from the receive task. A handle, or -1
 * when the table is full. May be called before `oh_sacn_start`; the socket opens
 * once the network is up. `sink` runs under the receiver's lock and must not
 * subscribe or unsubscribe.
 */
int oh_sacn_subscribe(uint16_t universe, oh_sacn_sink_t sink, void *ctx);
/* After this returns, `sink` is not running and will not be called again. */
void oh_sacn_unsubscribe(int handle);
/* The one consumer of frames (the DMX driver). NULL to detach. */
void oh_sacn_set_sink(oh_sacn_sink_t sink, void *ctx);
bool oh_sacn_listening(void);
uint16_t oh_sacn_universe(void);
uint32_t oh_sacn_frames(void);

#ifdef __cplusplus
}
#endif
