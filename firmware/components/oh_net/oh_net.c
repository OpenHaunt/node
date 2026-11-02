#include "oh_net.h"

#include <string.h>

#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "dhcpserver/dhcpserver.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "oh_config.h"
#include "oh_hostname.h"
#include "oh_node.h"
#include "sdkconfig.h"

static const char *TAG = "oh_net";

esp_err_t oh_captive_dns_start(void);
void oh_captive_dns_stop(void);

static struct {
    char hostname[32];      /* openhaunt-<sn>: the setup AP's SSID, and DHCP's without a name */
    char dhcp_name[33];     /* from the friendly name, "" for none */
    esp_netif_t *sta;
    esp_netif_t *ap;
    esp_netif_t *eth;
    oh_net_state_t state;
    bool ap_up;
    bool wifi_started;
    bool sta_wanted; /* credentials exist */
    int retries;
    esp_timer_handle_t sta_timeout; /* open the AP if the station is still down */
    esp_timer_handle_t reconnect;   /* retry the station */
    esp_timer_handle_t ap_close;    /* close the AP a while after the station got an IP */
    esp_timer_handle_t eth_fallback; /* start Wi-Fi if Ethernet never comes up */
    bool sntp_started;
    /* Whether each path is connected now. ESP-IDF keeps an address for
     * CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL after its link goes, so an address
     * alone does not say the path still carries anything. */
    bool eth_link;
    bool sta_link;
} s;

static void set_state(oh_net_state_t st)
{
    if (s.state == st) {
        return;
    }
    s.state = st;
    switch (st) {
    case OH_NET_UP:
        oh_node_post(OH_EVENT_NET_UP, NULL, 0);
        break;
    case OH_NET_SETUP_AP:
        oh_node_post(OH_EVENT_NET_SETUP_AP, NULL, 0);
        break;
    default:
        oh_node_post(OH_EVENT_NET_DOWN, NULL, 0);
        break;
    }
}

static bool has_ip(esp_netif_t *n)
{
    esp_netif_ip_info_t ip;
    return n && esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.ip.addr;
}

/* The path the node prefers: the cable while it is plugged in, else Wi-Fi. */
static esp_netif_t *active_netif(void)
{
    if (s.eth_link && has_ip(s.eth)) {
        return s.eth;
    }
    if (s.sta_link && has_ip(s.sta)) {
        return s.sta;
    }
    return NULL;
}

/*
 * Up while either path is. With Wi-Fi standing by beside the cable, one of them
 * going is not the node going: an access point rebooting must not take a cabled
 * node off the broker, nor a pulled cable one that Wi-Fi still carries. Both did,
 * when each path set the state on its own.
 */
static void refresh_state(void)
{
    set_state(active_netif() ? OH_NET_UP : (s.ap_up ? OH_NET_SETUP_AP : OH_NET_DOWN));
    oh_mdns_refresh_alias();
    oh_node_post(OH_EVENT_NET_CHANGED, NULL, 0);
}

/* What DHCP option 12 says: the friendly name when there is one, so the router's
 * client list and the switch port view that borrows it name the node. */
static const char *dhcp_hostname(void)
{
    return s.dhcp_name[0] ? s.dhcp_name : s.hostname;
}

/* ── SNTP ─────────────────────────────────────────────────────────────── */

static void sntp_start(void)
{
    if (s.sntp_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = true;
#if CONFIG_LWIP_DHCP_GET_NTP_SRV && CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    /* A venue router that hands out an NTP server (DHCP option 42) goes
     * into slot 0; the pool stays in slot 1 for when it does not. */
    cfg.server_from_dhcp = true;
    cfg.renew_servers_after_new_IP = true;
    cfg.index_of_first_server = 1;
#endif
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        s.sntp_started = true;
    }
}

/* ── Wi-Fi ────────────────────────────────────────────────────────────── */

#ifdef CONFIG_OH_TRANSPORT_WIFI_STA
#define HAVE_WIFI 1
#elif defined(CONFIG_OH_ETH_WIFI_FALLBACK)
#define HAVE_WIFI 1
#else
#define HAVE_WIFI 0
#endif

#if HAVE_WIFI

static esp_err_t wifi_init(void);

/* The setup AP hands out itself as the DNS server.
 *
 * IDF's DHCP server offers no DNS at all unless told to, and a phone given none
 * cannot ask the captive DNS anything: its portal check fails as "no internet"
 * rather than "sign in", no page comes up, and Android then sends everything —
 * 192.168.4.1 included — over mobile data. The option can only be set while the
 * server is stopped, so it is stopped, told, and started again. */
static void ap_offer_dns(void)
{
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s.ap, &ip) != ESP_OK) {
        return;
    }
    esp_netif_dhcps_stop(s.ap);
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4 = ip.ip;
    esp_err_t err = esp_netif_set_dns_info(s.ap, ESP_NETIF_DNS_MAIN, &dns);
    dhcps_offer_t offer = OFFER_DNS;
    if (err == ESP_OK) {
        err = esp_netif_dhcps_option(s.ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer,
                                     sizeof(offer));
    }
    esp_netif_dhcps_start(s.ap);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "setup AP offers no DNS (%s): no captive portal", esp_err_to_name(err));
    }
}

static void ap_open(void)
{
    if (s.ap_up || !CONFIG_OH_WIFI_SOFTAP_FALLBACK) {
        return;
    }
    wifi_config_t cfg = {0};
    snprintf((char *)cfg.ap.ssid, sizeof(cfg.ap.ssid), "%s", s.hostname);
    cfg.ap.ssid_len = strlen((char *)cfg.ap.ssid);
    cfg.ap.channel = 1;
    cfg.ap.authmode = WIFI_AUTH_OPEN;
    cfg.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    s.ap_up = true;
    ap_offer_dns();
    oh_captive_dns_start();
    ESP_LOGW(TAG, "setup access point \"%s\" is open (192.168.4.1)", s.hostname);
    set_state(OH_NET_SETUP_AP);
}

static void ap_close(void)
{
    if (!s.ap_up) {
        return;
    }
    oh_captive_dns_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    s.ap_up = false;
    ESP_LOGI(TAG, "setup access point closed");
}

static void sta_connect(void)
{
    oh_config_t cfg;
    oh_config_get(&cfg);
    s.sta_wanted = cfg.wifi_ssid[0] != '\0';
    if (!s.sta_wanted) {
        ESP_LOGW(TAG, "no Wi-Fi credentials");
        ap_open();
        return;
    }
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, cfg.wifi_ssid, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, cfg.wifi_psk, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = cfg.wifi_psk[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    if (s.state != OH_NET_SETUP_AP) {
        set_state(OH_NET_CONNECTING);
    }
    ESP_LOGI(TAG, "connecting to \"%s\"", cfg.wifi_ssid);
    esp_wifi_connect();
    if (!s.ap_up) {
        esp_timer_stop(s.sta_timeout);
        esp_timer_start_once(s.sta_timeout, (uint64_t)CONFIG_OH_WIFI_STA_TIMEOUT_S * 1000000);
    }
}

static void on_sta_timeout(void *arg)
{
    (void)arg;
    if (s.state != OH_NET_UP) {
        ESP_LOGW(TAG, "station not up after %d s", CONFIG_OH_WIFI_STA_TIMEOUT_S);
        ap_open();
    }
}

static void on_reconnect(void *arg)
{
    (void)arg;
    if (s.sta_wanted && s.state != OH_NET_UP) {
        esp_wifi_connect();
    }
}

static void on_ap_close(void *arg)
{
    (void)arg;
    if (s.state == OH_NET_UP) {
        ap_close();
    }
}

#ifdef CONFIG_OH_TRANSPORT_ETH
static void on_eth_fallback(void *arg)
{
    (void)arg;
    if (s.state != OH_NET_UP && !s.wifi_started) {
        ESP_LOGW(TAG, "no Ethernet address; falling back to Wi-Fi");
        wifi_init();
    }
}

#endif

static void schedule_reconnect(void)
{
    /* 1, 2, 4, 8 s, then every 8 s — and every 15 s while the AP is open so
     * scanning for the station does not make the AP unusable. */
    int delay_s = s.ap_up ? 15 : (s.retries < 3 ? 1 << s.retries : 8);
    s.retries++;
    esp_timer_stop(s.reconnect);
    esp_timer_start_once(s.reconnect, (uint64_t)delay_s * 1000000);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch (id) {
    case WIFI_EVENT_STA_START:
        sta_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "associated");
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "disconnected (reason %d)", d->reason);
        bool was_up = s.state == OH_NET_UP;
        s.sta_link = false;
        refresh_state();
        /* Still up means the cable carries the node: nothing to set up. */
        if (was_up && !oh_net_is_up() && CONFIG_OH_WIFI_REOPEN_AP_S > 0 && CONFIG_OH_WIFI_SOFTAP_FALLBACK) {
            esp_timer_stop(s.sta_timeout);
            esp_timer_start_once(s.sta_timeout, (uint64_t)CONFIG_OH_WIFI_REOPEN_AP_S * 1000000);
        }
        schedule_reconnect();
        break;
    }
    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "a client joined the setup AP");
        break;
    default:
        break;
    }
}

static void apply_static_ip(esp_netif_t *netif)
{
    oh_config_t cfg;
    oh_config_get(&cfg);
    if (!cfg.static_ip) {
        esp_netif_dhcpc_start(netif);
        return;
    }
    esp_netif_dhcpc_stop(netif);
    esp_netif_ip_info_t ip = {0};
    esp_netif_str_to_ip4(cfg.ip, &ip.ip);
    esp_netif_str_to_ip4(cfg.netmask, &ip.netmask);
    if (cfg.gateway[0]) {
        esp_netif_str_to_ip4(cfg.gateway, &ip.gw);
    }
    esp_netif_set_ip_info(netif, &ip);
    if (cfg.dns[0]) {
        esp_netif_dns_info_t dns = {0};
        esp_netif_str_to_ip4(cfg.dns, &dns.ip.u_addr.ip4);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    ESP_LOGI(TAG, "static ip %s/%s gw %s", cfg.ip, cfg.netmask, cfg.gateway);
}

static esp_err_t wifi_init(void)
{
    s.sta = esp_netif_create_default_wifi_sta();
    s.ap = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s.sta, dhcp_hostname());
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    apply_static_ip(s.sta);
#ifdef CONFIG_OH_TRANSPORT_ETH
    /* Standing by beside the cable, not instead of it: the default route stays on
     * the cable while it is plugged in. */
    esp_netif_set_route_prio(s.sta, 40);
#endif
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Latency over power: a relay following a button must not wait for DTIM. */
    esp_wifi_set_ps(WIFI_PS_NONE);
    s.wifi_started = true;
    return ESP_OK;
}

esp_err_t oh_net_apply_wifi(void)
{
    if (!s.wifi_started) {
        /* Credentials given to a node on its cable: Wi-Fi stands by from now on. */
        return oh_config_has_wifi() ? wifi_init() : ESP_ERR_INVALID_STATE;
    }
    s.retries = 0;
    esp_timer_stop(s.reconnect);
    esp_wifi_disconnect();
    s.sta_link = false;
    refresh_state();
    sta_connect();
    return ESP_OK;
}

cJSON *oh_net_wifi_scan(void)
{
    cJSON *list = cJSON_CreateArray();
    if (!s.wifi_started) {
        return list;
    }
    wifi_scan_config_t sc = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        return list;
    }
    uint16_t n = 20;
    wifi_ap_record_t *recs = calloc(n, sizeof(*recs));
    if (!recs) {
        esp_wifi_clear_ap_list();
        return list;
    }
    if (esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
        for (uint16_t i = 0; i < n; i++) {
            if (!recs[i].ssid[0]) {
                continue;
            }
            /* One row per SSID: the strongest. */
            bool dup = false;
            cJSON *row;
            cJSON_ArrayForEach(row, list) {
                if (strcmp(cJSON_GetObjectItem(row, "ssid")->valuestring, (char *)recs[i].ssid) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "ssid", (char *)recs[i].ssid);
            cJSON_AddNumberToObject(o, "rssi", recs[i].rssi);
            cJSON_AddBoolToObject(o, "open", recs[i].authmode == WIFI_AUTH_OPEN);
            cJSON_AddItemToArray(list, o);
        }
    }
    free(recs);
    return list;
}

#else /* !HAVE_WIFI */

esp_err_t oh_net_apply_wifi(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

cJSON *oh_net_wifi_scan(void)
{
    return cJSON_CreateArray();
}

#endif

/* ── IP events, shared ────────────────────────────────────────────────── */

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
        s.retries = 0;
#if HAVE_WIFI
        if (id == IP_EVENT_STA_GOT_IP) {
            s.sta_link = true;
        }
        esp_timer_stop(s.sta_timeout);
        esp_timer_stop(s.reconnect);
        if (s.ap_up) {
            /* Leave the AP up a moment so the setup page can say it worked. */
            esp_timer_stop(s.ap_close);
            esp_timer_start_once(s.ap_close, 15 * 1000000ULL);
        }
#endif
        refresh_state();
        sntp_start();
    } else if (id == IP_EVENT_STA_LOST_IP || id == IP_EVENT_ETH_LOST_IP) {
        ESP_LOGW(TAG, "lost ip");
        refresh_state();
    }
}

#ifdef CONFIG_OH_TRANSPORT_ETH
static void on_eth_link(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == ETHERNET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "cable unplugged");
        s.eth_link = false;
#if HAVE_WIFI
        /* With no credentials Wi-Fi is not standing by: the fallback timer only
         * ran once, at boot, so without this a node whose switch went away would
         * stay off the network. */
        if (!s.wifi_started) {
            esp_timer_stop(s.eth_fallback);
            esp_timer_start_once(s.eth_fallback, (uint64_t)CONFIG_OH_WIFI_STA_TIMEOUT_S * 1000000);
        }
#endif
        refresh_state();
    } else if (id == ETHERNET_EVENT_CONNECTED) {
        s.eth_link = true;
#if HAVE_WIFI
        esp_timer_stop(s.eth_fallback);
#endif
        refresh_state();
    }
}
#endif

esp_err_t oh_net_init(const char *hostname)
{
    strncpy(s.hostname, hostname, sizeof(s.hostname) - 1);
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event, NULL));
#if HAVE_WIFI
    esp_timer_create_args_t t1 = {.callback = on_sta_timeout, .name = "sta_timeout"};
    esp_timer_create_args_t t2 = {.callback = on_reconnect, .name = "reconnect"};
    esp_timer_create_args_t t3 = {.callback = on_ap_close, .name = "ap_close"};
    ESP_ERROR_CHECK(esp_timer_create(&t1, &s.sta_timeout));
    ESP_ERROR_CHECK(esp_timer_create(&t2, &s.reconnect));
    ESP_ERROR_CHECK(esp_timer_create(&t3, &s.ap_close));
#endif
#ifdef CONFIG_OH_TRANSPORT_ETH
#if HAVE_WIFI
    /* Before the cable's netif, which lwIP then lists first. A new connection into
     * the local subnet takes the first netif in that list with a link, so this is
     * what keeps the broker, the console and every reply on the cable while it is
     * plugged in, and puts them on Wi-Fi the moment it is not. */
    if (oh_config_has_wifi()) {
        wifi_init();
    }
#endif
    extern esp_err_t oh_eth_init(const char *hostname, esp_netif_t **netif);
    err = oh_eth_init(dhcp_hostname(), &s.eth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ethernet init failed: %s", esp_err_to_name(err));
    } else {
        ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_link, NULL));
    }
#if HAVE_WIFI
    /* With credentials, Wi-Fi stands by beside the cable from boot, so pulling the
     * cable moves the node rather than taking it off the network for the 30 s a
     * fallback waits, and a console can send to both paths at once. Without them
     * Wi-Fi is the fallback still: it starts, to open the setup AP, only if the
     * cable has not produced an address in time (or Ethernet failed to init). */
    esp_timer_create_args_t t4 = {.callback = on_eth_fallback, .name = "eth_fallback"};
    ESP_ERROR_CHECK(esp_timer_create(&t4, &s.eth_fallback));
    if (!s.wifi_started) {
        esp_timer_start_once(s.eth_fallback,
                             err == ESP_OK ? (uint64_t)CONFIG_OH_WIFI_STA_TIMEOUT_S * 1000000 : 1000000);
    }
    return ESP_OK;
#else
    return err;
#endif
#else
    return wifi_init();
#endif
}

esp_err_t oh_net_apply_ip(void)
{
#if HAVE_WIFI
    if (s.sta) {
        apply_static_ip(s.sta);
    }
#endif
    if (s.eth) {
        apply_static_ip(s.eth);
    }
    return ESP_OK;
}

/* In the TCP/IP task. A renew carries the new hostname, keeps the address and
 * does not drop a connection, so a router hears the name now rather than at half
 * the lease. Unbound (or static), the next DISCOVER carries it anyway. */
static esp_err_t renew(void *ctx)
{
    struct netif *n = esp_netif_get_netif_impl((esp_netif_t *)ctx);
    if (n && dhcp_supplied_address(n)) {
        dhcp_renew(n);
    }
    return ESP_OK;
}

void oh_net_set_dhcp_name(const char *name)
{
    char next[sizeof(s.dhcp_name)];
    oh_hostname_from_name(name, next, sizeof(next));
    if (strcmp(next, s.dhcp_name) == 0) {
        return;
    }
    strcpy(s.dhcp_name, next);
    ESP_LOGI(TAG, "DHCP hostname %s", dhcp_hostname());
    oh_mdns_set_alias(s.dhcp_name);
    esp_netif_t *const nifs[] = {s.eth, s.sta};
    for (size_t i = 0; i < 2; i++) {
        if (!nifs[i]) {
            continue; /* not up yet: it takes the name when it is created */
        }
        esp_netif_set_hostname(nifs[i], dhcp_hostname());
        esp_netif_dhcp_status_t st;
        if (esp_netif_dhcpc_get_status(nifs[i], &st) == ESP_OK && st == ESP_NETIF_DHCP_STARTED) {
            esp_netif_tcpip_exec(renew, nifs[i]);
        }
    }
}

void oh_net_dhcp_hostname(char *out, size_t cap)
{
    snprintf(out, cap, "%s", dhcp_hostname());
}

#ifdef CONFIG_OH_TRANSPORT_ETH
void oh_eth_lldp_set(const char *name, const char *desc);
bool oh_eth_lldp_neighbor(oh_lldp_neighbor_t *out, uint32_t *age_s);
#endif

void oh_net_set_lldp(const char *name, const char *desc)
{
#ifdef CONFIG_OH_TRANSPORT_ETH
    oh_eth_lldp_set(name, desc);
#else
    (void)name;
    (void)desc;
#endif
}

bool oh_net_lldp_neighbor(oh_lldp_neighbor_t *out, uint32_t *age_s)
{
#ifdef CONFIG_OH_TRANSPORT_ETH
    return s.eth && s.eth_link && oh_eth_lldp_neighbor(out, age_s);
#else
    (void)out;
    (void)age_s;
    return false;
#endif
}

oh_net_state_t oh_net_state(void)
{
    return s.state;
}

bool oh_net_is_up(void)
{
    return s.state == OH_NET_UP;
}

void oh_net_ifname(char *out, size_t cap)
{
    out[0] = '\0';
    char name[8];
    esp_netif_t *n = active_netif();
    if (n && esp_netif_get_netif_impl_name(n, name) == ESP_OK) {
        snprintf(out, cap, "%s", name);
    }
}

size_t oh_net_addresses(oh_net_address_t *out, size_t cap)
{
    size_t n = 0;
    esp_netif_t *first = active_netif();
    esp_netif_t *order[2] = {first, first == s.eth ? s.sta : s.eth};
    for (size_t i = 0; i < 2 && n < cap; i++) {
        esp_netif_t *nif = order[i];
        const bool live = nif && (nif == s.eth ? s.eth_link : s.sta_link);
        esp_netif_ip_info_t ip;
        if (!live || esp_netif_get_ip_info(nif, &ip) != ESP_OK || !ip.ip.addr) {
            continue;
        }
        out[n].transport = nif == s.eth ? "ethernet" : "wifi";
        snprintf(out[n].ip, sizeof(out[n].ip), IPSTR, IP2STR(&ip.ip));
        n++;
    }
    return n;
}

void oh_net_ip(char *out, size_t cap)
{
    out[0] = '\0';
    esp_netif_t *n = active_netif();
    if (!n && s.ap_up) {
        n = s.ap;
    }
    esp_netif_ip_info_t ip;
    if (n && esp_netif_get_ip_info(n, &ip) == ESP_OK) {
        snprintf(out, cap, IPSTR, IP2STR(&ip.ip));
    }
}

const char *oh_net_transport(void)
{
    esp_netif_t *n = active_netif();
    if (n && n == s.eth) {
        return "ethernet";
    }
    if (n) {
        return "wifi";
    }
    return s.ap_up ? "softap" : "none";
}

void oh_net_ssid(char *out, size_t cap)
{
    out[0] = '\0';
#if HAVE_WIFI
    wifi_ap_record_t ap;
    if (s.wifi_started && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(out, cap, "%s", (char *)ap.ssid);
    }
#endif
}

int oh_net_rssi(void)
{
#if HAVE_WIFI
    wifi_ap_record_t ap;
    if (s.wifi_started && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
#endif
    return 0;
}

void oh_net_ap_ssid(char *out, size_t cap)
{
    if (s.ap_up) {
        snprintf(out, cap, "%s", s.hostname);
    } else {
        out[0] = '\0';
    }
}
