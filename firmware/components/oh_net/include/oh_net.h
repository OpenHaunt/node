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

/* Re-read Wi-Fi credentials / IP settings from oh_config and apply them. */
esp_err_t oh_net_apply_wifi(void);
esp_err_t oh_net_apply_ip(void);

/* Blocking scan; returns [{ssid,rssi,auth}] sorted by rssi. */
cJSON *oh_net_wifi_scan(void);

/* mDNS: `_openhaunt._tcp` on port 80, instance and hostname openhaunt-<sn>. */
esp_err_t oh_mdns_start(const char *sn);
/* `desc` is the description's checksum (doc 14): a console fetches /api/v1/info
 * again when it changes. */
esp_err_t oh_mdns_update(const oh_module_t *module, const char *name, const char *fw, const char *sn,
                         const char *desc);

#ifdef __cplusplus
}
#endif
