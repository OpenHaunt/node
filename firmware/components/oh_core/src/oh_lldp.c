#include "oh_lldp.h"

#include <stdio.h>
#include <string.h>

const uint8_t OH_LLDP_MULTICAST[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};

enum {
    TLV_END = 0,
    TLV_CHASSIS_ID = 1,
    TLV_PORT_ID = 2,
    TLV_TTL = 3,
    TLV_PORT_DESC = 4,
    TLV_SYSTEM_NAME = 5,
    TLV_SYSTEM_DESC = 6,
    TLV_CAPABILITIES = 7,
    TLV_MGMT_ADDR = 8,
};

#define CHASSIS_MAC 4
#define CHASSIS_NETADDR 5
#define PORT_MAC 3
#define PORT_NETADDR 4
#define ADDR_IPV4 1
#define CAP_STATION 0x0080
#define TLV_MAX 511 /* nine length bits */
#define STR_MAX 255 /* what LLDP allows for its text TLVs */

/* ── Building ─────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t *p;
    size_t n, cap;
    bool overflow;
} writer_t;

static void put(writer_t *w, const void *data, size_t len)
{
    if (w->overflow || w->n + len > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->p + w->n, data, len);
    w->n += len;
}

static void put_u8(writer_t *w, uint8_t v)
{
    put(w, &v, 1);
}

static void put_u16(writer_t *w, uint16_t v)
{
    const uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    put(w, b, 2);
}

static void tlv_head(writer_t *w, unsigned type, size_t len)
{
    put_u16(w, (uint16_t)((type << 9) | (len & TLV_MAX)));
}

/* A text TLV, cut to 255 bytes without splitting a UTF-8 sequence. Empty or
 * missing text leaves the TLV out: a zero-length name says nothing. */
static void tlv_text(writer_t *w, unsigned type, const char *s)
{
    if (!s || !*s) {
        return;
    }
    size_t len = strlen(s);
    if (len > STR_MAX) {
        len = STR_MAX;
        while (len > 0 && ((unsigned char)s[len] & 0xC0) == 0x80) {
            len--;
        }
    }
    tlv_head(w, type, len);
    put(w, s, len);
}

size_t oh_lldp_build(const oh_lldp_self_t *self, uint8_t *frame, size_t cap)
{
    writer_t w = {.p = frame, .cap = cap};
    put(&w, OH_LLDP_MULTICAST, 6);
    put(&w, self->mac, 6);
    put_u16(&w, OH_LLDP_ETHERTYPE);

    tlv_head(&w, TLV_CHASSIS_ID, 7);
    put_u8(&w, CHASSIS_MAC);
    put(&w, self->mac, 6);
    tlv_head(&w, TLV_PORT_ID, 7);
    put_u8(&w, PORT_MAC);
    put(&w, self->mac, 6);
    tlv_head(&w, TLV_TTL, 2);
    put_u16(&w, self->ttl_s);

    tlv_text(&w, TLV_PORT_DESC, self->port_desc);
    tlv_text(&w, TLV_SYSTEM_NAME, self->system_name);
    tlv_text(&w, TLV_SYSTEM_DESC, self->system_desc);
    tlv_head(&w, TLV_CAPABILITIES, 4);
    put_u16(&w, CAP_STATION);
    put_u16(&w, CAP_STATION);
    if (self->ipv4) {
        /* Address string length, subtype, address; interface numbering "unknown",
         * interface 0; no OID. */
        tlv_head(&w, TLV_MGMT_ADDR, 12);
        put_u8(&w, 5);
        put_u8(&w, ADDR_IPV4);
        put(&w, &self->ipv4, 4);
        put_u8(&w, 1);
        put_u16(&w, 0);
        put_u16(&w, 0);
        put_u8(&w, 0);
    }
    tlv_head(&w, TLV_END, 0);

    /* Ethernet's minimum, which some MACs do not pad to on their own. */
    while (!w.overflow && w.n < 60) {
        put_u8(&w, 0);
    }
    return w.overflow ? 0 : w.n;
}

/* ── Parsing ──────────────────────────────────────────────────────────── */

/* Length of the valid UTF-8 sequence at p, 0 if it is not one. */
static size_t utf8_len(const uint8_t *p, size_t avail)
{
    size_t n;
    if (p[0] < 0x80) {
        return 1;
    } else if ((p[0] & 0xE0) == 0xC0 && p[0] >= 0xC2) {
        n = 2;
    } else if ((p[0] & 0xF0) == 0xE0) {
        n = 3;
    } else if ((p[0] & 0xF8) == 0xF0 && p[0] <= 0xF4) {
        n = 4;
    } else {
        return 0;
    }
    if (n > avail) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            return 0;
        }
    }
    return n;
}

/* Text as it goes into JSON: valid UTF-8 only, no control characters, cut on a
 * character boundary. */
static void copy_text(char *out, size_t cap, const uint8_t *p, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len;) {
        size_t c = utf8_len(p + i, len - i);
        if (c == 0 || (c == 1 && (p[i] < 0x20 || p[i] == 0x7F))) {
            i++;
            continue;
        }
        if (n + c >= cap) {
            break;
        }
        memcpy(out + n, p + i, c);
        n += c;
        i += c;
    }
    out[n] = '\0';
}

static void copy_ipv4(char *out, size_t cap, const uint8_t *a)
{
    snprintf(out, cap, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
}

/* A chassis or port ID: a subtype byte, then the ID. */
static void copy_id(char *out, size_t cap, const uint8_t *v, size_t len, uint8_t mac_subtype,
                    uint8_t netaddr_subtype)
{
    out[0] = '\0';
    if (len < 2) {
        return;
    }
    const uint8_t sub = v[0];
    if (sub == mac_subtype && len == 7) {
        snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x", v[1], v[2], v[3], v[4], v[5], v[6]);
    } else if (sub == netaddr_subtype && len == 6 && v[1] == ADDR_IPV4) {
        copy_ipv4(out, cap, v + 2);
    } else {
        copy_text(out, cap, v + 1, len - 1);
    }
}

bool oh_lldp_parse(const uint8_t *frame, size_t len, oh_lldp_neighbor_t *out)
{
    if (!frame || len < 14) {
        return false;
    }
    size_t off = 12;
    uint16_t type = (uint16_t)(frame[off] << 8 | frame[off + 1]);
    if (type == 0x8100 && len >= 18) {
        off += 4; /* an 802.1Q tag */
        type = (uint16_t)(frame[off] << 8 | frame[off + 1]);
    }
    if (type != OH_LLDP_ETHERTYPE) {
        return false;
    }
    off += 2;

    memset(out, 0, sizeof(*out));
    unsigned index = 0;
    bool ended = false;
    while (off + 2 <= len) {
        const unsigned t = frame[off] >> 1;
        const size_t l = (size_t)(frame[off] & 1) << 8 | frame[off + 1];
        off += 2;
        if (off + l > len) {
            return false;
        }
        const uint8_t *v = frame + off;
        off += l;
        /* Chassis ID, Port ID, TTL: first, in that order, each once. */
        if (index < 3 && t != index + 1) {
            return false;
        }
        index++;
        switch (t) {
        case TLV_END:
            ended = true;
            break;
        case TLV_CHASSIS_ID:
            if (index != 1) {
                return false;
            }
            copy_id(out->chassis_id, sizeof(out->chassis_id), v, l, CHASSIS_MAC, CHASSIS_NETADDR);
            break;
        case TLV_PORT_ID:
            if (index != 2) {
                return false;
            }
            copy_id(out->port_id, sizeof(out->port_id), v, l, PORT_MAC, PORT_NETADDR);
            break;
        case TLV_TTL:
            if (index != 3 || l < 2) {
                return false;
            }
            out->ttl_s = (uint16_t)(v[0] << 8 | v[1]);
            break;
        case TLV_PORT_DESC:
            copy_text(out->port_desc, sizeof(out->port_desc), v, l);
            break;
        case TLV_SYSTEM_NAME:
            copy_text(out->system_name, sizeof(out->system_name), v, l);
            break;
        case TLV_SYSTEM_DESC:
            copy_text(out->system_desc, sizeof(out->system_desc), v, l);
            break;
        case TLV_MGMT_ADDR:
            /* The first IPv4 one; a switch may list several addresses. */
            if (!out->mgmt_ipv4[0] && l >= 6 && v[0] == 5 && v[1] == ADDR_IPV4) {
                copy_ipv4(out->mgmt_ipv4, sizeof(out->mgmt_ipv4), v + 2);
            }
            break;
        default:
            break; /* capabilities, organisationally specific (LLDP-MED, 802.3) */
        }
        if (ended) {
            break;
        }
    }
    /* Some senders stop at the frame's end without an End TLV; the three
     * mandatory ones are what make it LLDP. */
    return index >= 3;
}
