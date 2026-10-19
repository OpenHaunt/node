/* Enough of E1.31 (sACN) to tell a data frame from a stray datagram. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OH_E131_PORT 5568

typedef struct {
    uint16_t universe;
    uint8_t sequence;
    uint8_t priority;
    uint8_t start_code;
    const uint8_t *slots; /* channel data after the start code */
    uint16_t n_slots;     /* 0..512 */
    uint8_t cid[16];      /* the sender's component id: what tells two sources apart */
    uint8_t options;      /* OH_E131_OPT_* */
} oh_e131_frame_t;

/* Framing-layer option bits. */
#define OH_E131_OPT_PREVIEW 0x80    /* Preview_Data: for a visualiser, not for a light */
#define OH_E131_OPT_TERMINATED 0x40 /* Stream_Terminated: this source is going away */

bool oh_e131_parse(const uint8_t *packet, size_t len, oh_e131_frame_t *out);

/* 239.255.hi.lo for a universe, as a host-order IPv4. */
uint32_t oh_e131_multicast_group(uint16_t universe);

#ifdef __cplusplus
}
#endif
