// W5500 Ethernet through the ESP-IDF driver: interrupt-driven, part of the lwIP stack, so every
// socket (HTTP, UDP/RTP, mDNS) works on Ethernet and Wi-Fi alike.
#pragma once

#include <IPAddress.h>
#include <stdint.h>

enum class EthState : uint8_t { HwError, NoLink, Dhcp, DhcpFailed, Online };

struct EthPins {
  int sclk, mosi, miso, cs, irq, rst;
};

// Call after Wi-Fi is set up (needs esp_netif and the default event loop). Returns false if the W5500
// doesn't answer; ethState() then reports HwError.
bool ethBegin(const EthPins &pins, const uint8_t mac[6], const char *hostname);

EthState ethState();
IPAddress ethLocalIP();
// negotiated link: 10 or 100 Mbit/s and full duplex; false while the link is down
bool ethLinkInfo(int &mbps, bool &fullDuplex);
