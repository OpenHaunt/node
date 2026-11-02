/*
 * How the node reaches the network: Ethernet on the base board, Wi-Fi
 * station on the devkits, and the setup access point that opens when the
 * station cannot connect. Plus mDNS, SNTP and the node's own clock.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"
#include "oh_lldp.h"
#include "oh_module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OH_NET_DOWN = 0,   /* no link / not associated */
    OH_NET_CONNECTING, /* station trying */
    OH_NET_UP,         /* has an IP */
    OH_NET_SETUP_AP,   /* setup access point open, station still trying underneath */
} oh_net_state_t;

esp_err_t oh_net_init(const char *hostname);
oh_net_state_t oh_net_state(void);
bool oh_net_is_up(void);
/* Dotted IPv4 of the active interface, "" when down. */
void oh_net_ip(char *out, size_t cap);
const char *oh_net_transport(void); /* "ethernet" | "wifi" | "softap" */

typedef struct {
    const char *transport; /* "ethernet" | "wifi" */
    char ip[16];
} oh_net_address_t;

/* lwIP's name for the preferred path ("en1", "st1"), or "" with none. */
void oh_net_ifname(char *out, size_t cap);

/* Every path the node can be reached by now, the preferred one first. */
size_t oh_net_addresses(oh_net_address_t *out, size_t cap);
void oh_net_ssid(char *out, size_t cap);
int oh_net_rssi(void);
void oh_net_ap_ssid(char *out, size_t cap);

/* The friendly name as the router should see it: folded into DHCP option 12
 * (oh_hostname.h) and renewed at once. "" (no name of the user's) is
 * openhaunt-<sn>. Callable before oh_net_init, which then starts with it. */
void oh_net_set_dhcp_name(const char *name);
void oh_net_dhcp_hostname(char *out, size_t cap);

/* LLDP's System Name and Description, sent on the cable every 30 s and at once
 * when either changes. Nothing on a node without Ethernet. */
void oh_net_set_lldp(const char *name, const char *desc);
/* The switch on the other end of the cable, as its LLDP last said, and how many
 * seconds ago. False with none heard since the link came up, or its TTL run out. */
bool oh_net_lldp_neighbor(oh_lldp_neighbor_t *out, uint32_t *age_s);

/* Re-read Wi-Fi credentials / IP settings from oh_config and apply them. */
esp_err_t oh_net_apply_wifi(void);
esp_err_t oh_net_apply_ip(void);

/* Blocking scan; returns [{ssid,rssi,auth}] sorted by rssi. */
cJSON *oh_net_wifi_scan(void);

/* mDNS: `_openhaunt._tcp` on port 80, instance and hostname openhaunt-<sn>. */
esp_err_t oh_mdns_start(const char *sn);
/* A second hostname beside openhaunt-<sn> ("" for none): the friendly name, as
 * oh_net_set_dhcp_name folds it. Set before oh_mdns_start, it is applied there. */
void oh_mdns_set_alias(const char *alias);
/* Hand the alias the node's current addresses; oh_net calls it on every change. */
void oh_mdns_refresh_alias(void);
/* `desc` is the description's checksum (doc 14): a console fetches /api/v1/info
 * again when it changes. */
esp_err_t oh_mdns_update(const oh_module_t *module, const char *name, const char *fw, const char *sn,
                         const char *desc);

#ifdef __cplusplus
}
#endif
