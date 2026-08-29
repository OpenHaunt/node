#include "oh_e131.h"

#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

bool oh_e131_parse(const uint8_t *packet, size_t len, oh_e131_frame_t *out)
{
    /* Root layer: preamble size, postamble size, "ASC-E1.17\0\0\0". */
    if (len < 126 || memcmp(packet + 4, "ASC-E1.17\0\0\0", 12) != 0) {
        return false;
    }
    /* Root vector VECTOR_ROOT_E131_DATA = 0x00000004 at 18..21. */
    if (packet[18] != 0 || packet[19] != 0 || packet[20] != 0 || packet[21] != 0x04) {
        return false;
    }
    /* Framing vector VECTOR_E131_DATA_PACKET = 0x00000002 at 40..43. */
    if (packet[40] != 0 || packet[41] != 0 || packet[42] != 0 || packet[43] != 0x02) {
        return false;
    }
    /* DMP vector 0x02 at 117. */
    if (packet[117] != 0x02) {
        return false;
    }
    uint16_t count = be16(packet + 123); /* property value count, includes start code */
    if (count == 0 || count > 513 || (size_t)125 + count > len) {
        return false;
    }
    out->priority = packet[108];
    out->sequence = packet[111];
    out->universe = be16(packet + 113);
    out->start_code = packet[125];
    out->slots = packet + 126;
    out->n_slots = (uint16_t)(count - 1);
    return true;
}

uint32_t oh_e131_multicast_group(uint16_t universe)
{
    return (239u << 24) | (255u << 16) | ((uint32_t)(universe >> 8) << 8) | (universe & 0xFF);
}
