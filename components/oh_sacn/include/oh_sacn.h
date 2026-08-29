/*
 * E1.31 (sACN) reception for a node that forwards a universe: UDP 5568,
 * unicast and the universe's multicast group, opened only when the bound
 * module describes `dmx` and a controller has configured a universe.
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
/* The one consumer of frames (the DMX driver). NULL to detach. */
void oh_sacn_set_sink(oh_sacn_sink_t sink, void *ctx);
bool oh_sacn_listening(void);
uint16_t oh_sacn_universe(void);
uint32_t oh_sacn_frames(void);

#ifdef __cplusplus
}
#endif
