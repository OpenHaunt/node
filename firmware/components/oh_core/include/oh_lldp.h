/*
 * LLDP (IEEE 802.1AB), both ways, as bytes.
 *
 * Sent: the node's friendly name as System Name, so a managed switch's neighbour
 * table names the node on its port. Received: what the switch says about itself,
 * so /status can say "usw-24, port 7" when someone is tracing a cable.
 *
 * Only frames: when to send and where they come from is the Ethernet driver's.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OH_LLDP_ETHERTYPE 0x88CC
/* Nearest-bridge group address: no 802.1D bridge forwards it, so a frame sent
 * there reaches the switch port and nothing past it. */
extern const uint8_t OH_LLDP_MULTICAST[6];

/* An LLDPDU is at most 1500 bytes; ours stays far below. */
#define OH_LLDP_FRAME_MAX 600

typedef struct {
    uint8_t mac[6];          /* chassis and port ID, and the frame's source */
    const char *system_name; /* the friendly name */
    const char *system_desc; /* "OpenHaunt node, <module>, fw <x>" */
    const char *port_desc;
    uint32_t ipv4;           /* network byte order; 0 leaves the management address out */
    uint16_t ttl_s;          /* 0 announces the node is going away */
} oh_lldp_self_t;

/* The whole Ethernet frame, header included. Returns its length, 0 if `cap` is
 * too small. Strings longer than LLDP's 255 bytes are cut. */
size_t oh_lldp_build(const oh_lldp_self_t *self, uint8_t *frame, size_t cap);

#define OH_LLDP_STR 64

typedef struct {
    /* As text: a MAC subtype is rendered "aa:bb:cc:dd:ee:ff", a network address as
     * dotted IPv4, anything else as its printable bytes. */
    char chassis_id[OH_LLDP_STR];
    char port_id[OH_LLDP_STR];
    char port_desc[OH_LLDP_STR];
    char system_name[OH_LLDP_STR];
    char system_desc[OH_LLDP_STR];
    char mgmt_ipv4[16]; /* "" without an IPv4 management address */
    uint16_t ttl_s;
} oh_lldp_neighbor_t;

/* Parses a whole Ethernet frame. False when it is not LLDP or not well formed:
 * the three mandatory TLVs first and in order, no TLV running past the end. */
bool oh_lldp_parse(const uint8_t *frame, size_t len, oh_lldp_neighbor_t *out);

#ifdef __cplusplus
}
#endif
