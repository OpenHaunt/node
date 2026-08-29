/*
 * The base board's Ethernet: internal EMAC, RMII, RTL8201 PHY, a 50 MHz
 * oscillator whose enable pin is held low until after strapping so that
 * GPIO0 can be a boot pin and a REF_CLK input at the same time.
 */
#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "oh_node.h"
#include "sdkconfig.h"

#ifdef CONFIG_OH_TRANSPORT_ETH_RMII

static const char *TAG = "oh_eth";
static esp_eth_handle_t s_eth;

static void eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "link down");
        oh_node_post(OH_EVENT_NET_DOWN, NULL, 0);
        break;
    default:
        break;
    }
}

esp_err_t oh_eth_init(const char *hostname, esp_netif_t **netif_out)
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
    esp_err_t err = esp_eth_driver_install(&cfg, &s_eth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "driver install: %s", esp_err_to_name(err));
        return err;
    }
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netif_cfg);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(s_eth)));
    esp_netif_set_hostname(netif, hostname);
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event, NULL));
    *netif_out = netif;
    return esp_eth_start(s_eth);
}

#endif
