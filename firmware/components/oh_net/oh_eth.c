/*
 * Ethernet, two ways.
 *
 * The base board: internal EMAC, RMII, RTL8201 PHY, a 50 MHz oscillator whose
 * enable pin is held low until after strapping so that GPIO0 can be a boot pin
 * and a REF_CLK input at the same time.
 *
 * The V0 prototype: a W5500 on SPI. It has no MAC address of its own, so it
 * is given the chip's Ethernet MAC from efuse; without that every node would
 * come up as the driver's placeholder address and the switch would see one
 * host moving between ports.
 *
 * Both say who they are to the switch over LLDP and listen to what it says back
 * (oh_lldp.h): a frame every 30 s, and one at once when the name changes or the
 * cable goes in.
 */
#include <string.h>

#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "oh_lldp.h"
#include "oh_node.h"
#include "sdkconfig.h"
#ifdef CONFIG_OH_TRANSPORT_ETH_RMII
#include "esp_eth_mac_esp.h"
#endif
#ifdef CONFIG_OH_TRANSPORT_ETH_W5500
#include "driver/spi_master.h"
#include "esp_eth_mac_spi.h"
#endif

#ifdef CONFIG_OH_TRANSPORT_ETH

static const char *TAG = "oh_eth";
static esp_eth_handle_t s_eth;
static esp_netif_t *s_netif;

/* ── LLDP ─────────────────────────────────────────────────────────────── */

#define LLDP_INTERVAL_S 30
/* 802.1AB's default: four intervals, so a switch forgets the node two minutes
 * after it goes quiet. */
#define LLDP_TTL_S 120

static struct {
    portMUX_TYPE lock;
    char name[OH_LLDP_STR];
    char desc[OH_LLDP_STR];
    esp_timer_handle_t timer;
    uint8_t mac[6];
    /* Our MAC filter could not take the LLDP group address (the W5500 has none
     * for anything but IP multicast), so the chip takes every frame and
     * eth_input drops the unicast ones meant for someone else. */
    bool promiscuous;
    oh_lldp_neighbor_t neighbor;
    int64_t heard_us; /* 0: nothing heard since the link came up */
} s_lldp = {.lock = portMUX_INITIALIZER_UNLOCKED};

static void lldp_send(void)
{
    oh_lldp_self_t self = {.port_desc = "ethernet", .ttl_s = LLDP_TTL_S};
    char name[OH_LLDP_STR], desc[OH_LLDP_STR];
    portENTER_CRITICAL(&s_lldp.lock);
    memcpy(name, s_lldp.name, sizeof(name));
    memcpy(desc, s_lldp.desc, sizeof(desc));
    portEXIT_CRITICAL(&s_lldp.lock);
    if (!name[0]) {
        return; /* not told yet: oh_app_announce comes right after init */
    }
    memcpy(self.mac, s_lldp.mac, 6);
    self.system_name = name;
    self.system_desc = desc;
    esp_netif_ip_info_t ip;
    if (s_netif && esp_netif_get_ip_info(s_netif, &ip) == ESP_OK) {
        self.ipv4 = ip.ip.addr;
    }
    uint8_t frame[OH_LLDP_FRAME_MAX];
    const size_t n = oh_lldp_build(&self, frame, sizeof(frame));
    if (n == 0) {
        return;
    }
    /* Refused while the link is down, which is fine: link up sends again. */
    esp_eth_transmit(s_eth, frame, n);
}

static void on_lldp_timer(void *arg)
{
    (void)arg;
    lldp_send();
}

void oh_eth_lldp_set(const char *name, const char *desc)
{
    bool changed;
    portENTER_CRITICAL(&s_lldp.lock);
    changed = strncmp(s_lldp.name, name, sizeof(s_lldp.name) - 1) != 0 ||
              strncmp(s_lldp.desc, desc, sizeof(s_lldp.desc) - 1) != 0;
    strlcpy(s_lldp.name, name, sizeof(s_lldp.name));
    strlcpy(s_lldp.desc, desc, sizeof(s_lldp.desc));
    portEXIT_CRITICAL(&s_lldp.lock);
    if (changed && s_eth) {
        lldp_send();
    }
}

bool oh_eth_lldp_neighbor(oh_lldp_neighbor_t *out, uint32_t *age_s)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lldp.lock);
    const int64_t heard = s_lldp.heard_us;
    const bool fresh = heard && now - heard < (int64_t)s_lldp.neighbor.ttl_s * 1000000;
    if (fresh) {
        *out = s_lldp.neighbor;
    }
    portEXIT_CRITICAL(&s_lldp.lock);
    if (fresh && age_s) {
        *age_s = (uint32_t)((now - heard) / 1000000);
    }
    return fresh;
}

static void lldp_forget(void)
{
    portENTER_CRITICAL(&s_lldp.lock);
    s_lldp.heard_us = 0;
    portEXIT_CRITICAL(&s_lldp.lock);
}

/*
 * Every received frame passes here before lwIP, which would drop LLDP anyway.
 * Runs in the driver's receive task and owns `buffer`: whatever is not handed to
 * esp_netif_receive is freed.
 */
static esp_err_t eth_input(esp_eth_handle_t eth, uint8_t *buffer, uint32_t length, void *priv)
{
    (void)eth;
    if (length >= 14) {
        const uint16_t type = (uint16_t)(buffer[12] << 8 | buffer[13]);
        const bool tagged_lldp = type == 0x8100 && length >= 18 &&
                                 (uint16_t)(buffer[16] << 8 | buffer[17]) == OH_LLDP_ETHERTYPE;
        if (type == OH_LLDP_ETHERTYPE || tagged_lldp) {
            oh_lldp_neighbor_t nb;
            if (oh_lldp_parse(buffer, length, &nb)) {
                portENTER_CRITICAL(&s_lldp.lock);
                s_lldp.neighbor = nb;
                /* TTL 0 is a switch saying it is going away. */
                s_lldp.heard_us = nb.ttl_s ? esp_timer_get_time() : 0;
                portEXIT_CRITICAL(&s_lldp.lock);
            }
            free(buffer);
            return ESP_OK;
        }
        if (s_lldp.promiscuous && !(buffer[0] & 1) && memcmp(buffer, s_lldp.mac, 6) != 0) {
            free(buffer);
            return ESP_OK;
        }
    }
    return esp_netif_receive((esp_netif_t *)priv, buffer, length, NULL);
}

static void lldp_start(void)
{
    esp_eth_ioctl(s_eth, ETH_CMD_G_MAC_ADDR, s_lldp.mac);
    uint8_t group[6];
    memcpy(group, OH_LLDP_MULTICAST, 6);
#ifdef CONFIG_OH_TRANSPORT_ETH_W5500
    /* Its driver refuses anything but IP multicast, and logs two errors saying so. */
    const bool filtered = false;
#else
    const bool filtered = esp_eth_ioctl(s_eth, ETH_CMD_ADD_MAC_FILTER, group) == ESP_OK;
#endif
    if (!filtered) {
        bool on = true;
        esp_err_t err = esp_eth_ioctl(s_eth, ETH_CMD_S_PROMISCUOUS, &on);
        s_lldp.promiscuous = err == ESP_OK;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "LLDP: cannot receive the group address (%s); sending only", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "LLDP: receiving in promiscuous mode, foreign unicast dropped before lwIP");
        }
    }
    /* After esp_netif_attach, which set the glue's own input path: this one
     * replaces it and passes everything that is not LLDP on to the same netif.
     * (The glue's version also feeds the L2 TAP, which this project leaves off.) */
    ESP_ERROR_CHECK(esp_eth_update_input_path(s_eth, eth_input, s_netif));
    esp_timer_create_args_t t = {.callback = on_lldp_timer, .name = "lldp"};
    ESP_ERROR_CHECK(esp_timer_create(&t, &s_lldp.timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lldp.timer, (uint64_t)LLDP_INTERVAL_S * 1000000));
}

static void eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "link up");
        /* The switch learns the name now rather than up to 30 s from now. */
        lldp_send();
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "link down");
        /* The cable may come back in another port. */
        lldp_forget();
        oh_node_post(OH_EVENT_NET_DOWN, NULL, 0);
        break;
    default:
        break;
    }
}

#ifdef CONFIG_OH_TRANSPORT_ETH_RMII
static esp_err_t new_driver(void)
{
    if (CONFIG_OH_ETH_OSC_EN_GPIO >= 0) {
        gpio_config_t osc = {.pin_bit_mask = 1ULL << CONFIG_OH_ETH_OSC_EN_GPIO, .mode = GPIO_MODE_OUTPUT};
        gpio_config(&osc);
        gpio_set_level(CONFIG_OH_ETH_OSC_EN_GPIO, 0);
    }

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac.smi_gpio.mdc_num = CONFIG_OH_ETH_MDC_GPIO;
    emac.smi_gpio.mdio_num = CONFIG_OH_ETH_MDIO_GPIO;
    emac.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO;

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_OH_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_OH_ETH_PHY_RESET_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_config);
    if (!mac || !phy) {
        return ESP_FAIL;
    }

    /* Strapping was sampled long ago; the PHY needs its clock before the
     * driver talks to it. */
    if (CONFIG_OH_ETH_OSC_EN_GPIO >= 0) {
        gpio_set_level(CONFIG_OH_ETH_OSC_EN_GPIO, 1);
    }

    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
    /* 2 s by default. Pulling the cable is how a stream moves to wifi, and every
     * moment of not knowing is sound sent down a dead cable. */
    cfg.check_link_period_ms = 100;
    esp_err_t err = esp_eth_driver_install(&cfg, &s_eth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "driver install: %s", esp_err_to_name(err));
    }
    return err;
}
#endif

#ifdef CONFIG_OH_TRANSPORT_ETH_W5500
static esp_err_t new_driver(void)
{
    /* gpio_in may have installed it already; either way the W5500's INTn needs it. */
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    spi_bus_config_t bus = {
        .miso_io_num = CONFIG_OH_W5500_MISO_GPIO,
        .mosi_io_num = CONFIG_OH_W5500_MOSI_GPIO,
        .sclk_io_num = CONFIG_OH_W5500_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    err = spi_bus_initialize(CONFIG_OH_W5500_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi bus: %s", esp_err_to_name(err));
        return err;
    }
    spi_device_interface_config_t dev = {
        .mode = 0,
        .clock_speed_hz = CONFIG_OH_W5500_CLOCK_MHZ * 1000 * 1000,
        .queue_size = 20,
        .spics_io_num = CONFIG_OH_W5500_CS_GPIO,
    };
    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(CONFIG_OH_W5500_SPI_HOST, &dev);
    w5500.int_gpio_num = CONFIG_OH_W5500_INT_GPIO;
    /* Without INTn the driver polls; 10 ms keeps a button press under a frame. */
    w5500.poll_period_ms = CONFIG_OH_W5500_INT_GPIO < 0 ? 10 : 0;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = CONFIG_OH_W5500_RST_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (!mac || !phy) {
        return ESP_FAIL;
    }
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
    /* 2 s by default. Pulling the cable is how a stream moves to wifi, and every
     * moment of not knowing is sound sent down a dead cable. */
    cfg.check_link_period_ms = 100;
    err = esp_eth_driver_install(&cfg, &s_eth);
    if (err != ESP_OK) {
        /* The usual reason on a new board: nothing answering on the SPI bus. */
        ESP_LOGE(TAG, "W5500 driver install: %s (is the chip answering on SPI?)", esp_err_to_name(err));
        return err;
    }
    uint8_t addr[6];
    esp_read_mac(addr, ESP_MAC_ETH);
    err = esp_eth_ioctl(s_eth, ETH_CMD_S_MAC_ADDR, addr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "W5500 mac address: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "W5500 on SPI%d at %d MHz, mac %02x:%02x:%02x:%02x:%02x:%02x", CONFIG_OH_W5500_SPI_HOST + 1,
             CONFIG_OH_W5500_CLOCK_MHZ, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    return ESP_OK;
}
#endif

esp_err_t oh_eth_init(const char *hostname, esp_netif_t **netif_out)
{
    esp_err_t err = new_driver();
    if (err != ESP_OK) {
        return err;
    }
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netif_cfg);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(s_eth)));
    esp_netif_set_hostname(netif, hostname);
    s_netif = netif;
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event, NULL));
    *netif_out = netif;
    /* Before the driver runs: the input path is swapped without a lock. */
    lldp_start();
    return esp_eth_start(s_eth);
}

#endif
