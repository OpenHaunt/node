#include "eth.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>

constexpr uint32_t SPI_CLOCK_HZ = 20 * 1000 * 1000;
// link up but no lease after this long: shown as "DHCP failed" (lwIP keeps retrying)
constexpr uint32_t DHCP_TIMEOUT_MS = 15000;

// local W5500 MAC driver with fixed receive loop (src/w5500_mac.c)
extern "C" esp_eth_mac_t *oh_eth_mac_new_w5500(const eth_w5500_config_t *w5500_config,
                                               const eth_mac_config_t *mac_config);

static esp_netif_t *netif;
static esp_eth_handle_t ethHandle;
static volatile bool hwOk = false;
static volatile bool linkUp = false;
static volatile bool gotIp = false;
static volatile uint32_t linkUpAt = 0;

static void onEthEvent(void *, esp_event_base_t, int32_t id, void *) {
  if (id == ETHERNET_EVENT_CONNECTED) {
    linkUp = true;
    linkUpAt = millis();
  } else if (id == ETHERNET_EVENT_DISCONNECTED) {
    linkUp = false;
    gotIp = false;
  }
}

static void onIpEvent(void *, esp_event_base_t, int32_t id, void *) {
  gotIp = id == IP_EVENT_ETH_GOT_IP;
}

bool ethBegin(const EthPins &pins, const uint8_t mac[6], const char *hostname) {
  // Wi-Fi already created both; the calls just make sure, errors for "exists" are expected
  esp_netif_init();
  esp_event_loop_create_default();
  gpio_install_isr_service(0);

  esp_netif_config_t netifCfg = ESP_NETIF_DEFAULT_ETH();
  netif = esp_netif_new(&netifCfg);
  esp_netif_set_hostname(netif, hostname);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = pins.mosi;
  bus.miso_io_num = pins.miso;
  bus.sclk_io_num = pins.sclk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  // W5500 frame: 16 bit address phase + 8 bit control phase, mapped onto command/address bits
  spi_device_interface_config_t dev = {};
  dev.command_bits = 16;
  dev.address_bits = 8;
  dev.mode = 0;
  dev.clock_speed_hz = SPI_CLOCK_HZ;
  dev.spics_io_num = pins.cs;
  dev.queue_size = 20;
  spi_device_handle_t spi;
  if (spi_bus_add_device(SPI2_HOST, &dev, &spi) != ESP_OK) return false;

  eth_w5500_config_t w5500Cfg = ETH_W5500_DEFAULT_CONFIG(spi);
  w5500Cfg.int_gpio_num = pins.irq;
  eth_mac_config_t macCfg = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phyCfg = ETH_PHY_DEFAULT_CONFIG();
  phyCfg.phy_addr = 1;
  phyCfg.reset_gpio_num = pins.rst;

  esp_eth_mac_t *ethMac = oh_eth_mac_new_w5500(&w5500Cfg, &macCfg);
  esp_eth_phy_t *ethPhy = esp_eth_phy_new_w5500(&phyCfg);
  esp_eth_config_t ethCfg = ETH_DEFAULT_CONFIG(ethMac, ethPhy);
  esp_eth_handle_t eth = nullptr;
  if (esp_eth_driver_install(&ethCfg, &eth) != ESP_OK) return false;  // W5500 not answering

  esp_eth_ioctl(eth, ETH_CMD_S_MAC_ADDR, const_cast<uint8_t *>(mac));
  esp_netif_attach(netif, esp_eth_new_netif_glue(eth));
  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, onEthEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, onIpEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, onIpEvent, nullptr);
  esp_eth_start(eth);
  ethHandle = eth;
  hwOk = true;
  return true;
}

EthState ethState() {
  if (!hwOk) return EthState::HwError;
  if (!linkUp) return EthState::NoLink;
  if (gotIp) return EthState::Online;
  return millis() - linkUpAt > DHCP_TIMEOUT_MS ? EthState::DhcpFailed : EthState::Dhcp;
}

bool ethLinkInfo(int &mbps, bool &fullDuplex) {
  if (!hwOk || !linkUp) return false;
  eth_speed_t speed;
  eth_duplex_t duplex;
  if (esp_eth_ioctl(ethHandle, ETH_CMD_G_SPEED, &speed) != ESP_OK) return false;
  if (esp_eth_ioctl(ethHandle, ETH_CMD_G_DUPLEX_MODE, &duplex) != ESP_OK) return false;
  mbps = speed == ETH_SPEED_100M ? 100 : 10;
  fullDuplex = duplex == ETH_DUPLEX_FULL;
  return true;
}

IPAddress ethLocalIP() {
  esp_netif_ip_info_t info;
  if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK) return IPAddress();
  return IPAddress(info.ip.addr);
}
