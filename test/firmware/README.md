# V0.1 test firmware

Bring-up and test firmware for the **V0.1 board** (`hardware/OH_Node_V0_Prototype` at tag `v0.1`,
ESP32-S3-WROOM-1-N16R2). PlatformIO, Arduino framework (arduino-esp32 2.0.7 / ESP-IDF 4.4.7).

Covers Ethernet (W5500 via the ESP-IDF driver), Wi-Fi with mDNS `oh-node.local`, the four relays, audio
(I2S, tone/gong, RTP live stream), both WS2812/WS2815 outputs, both PIR inputs with actions, a German web UI
with live updates (SSE) and OTA updates. No authentication on any endpoint.

## Build and flash

```bash
pio run -e oh_node -t upload       # first flash over native USB (hold SW1/BOOT, power-cycle)
pio run -e oh_node_ota -t upload   # update over the network
```

`oh_node_ota` tries two hard-coded IPs (Ethernet, then Wi-Fi) from the bring-up network; change them in
`platformio.ini`. The USB port in `upload_port` is machine-specific too.

## Source

| File | Contents |
|---|---|
| `src/main.cpp` | Setup, web UI, HTTP API, relays, Wi-Fi, OTA, crash-loop safe mode |
| `src/eth.cpp`, `src/w5500_mac.c` | W5500 on `esp_eth`. Patched driver copy: stock IDF 4.4.7 sets the socket 0 RX buffer before zeroing sockets 1–7, so the W5500 keeps 2 KB and drops back-to-back frames |
| `src/sse.cpp` | Server-sent events (`GET /api/events`) |
| `src/led.cpp` | WS2812/WS2815 via RMT: off, color, rainbow, chase, sparkle, test |
| `src/pir.cpp`, `src/actions.cpp` | PIR inputs, per-PIR actions (LED overlay, gong) |
| `src/audio.cpp`, `src/rtp.cpp` | I2S output, RTP/UDP 5004 L16 48 kHz stereo with jitter buffer |
| `tools/rtp_send.py` | Paced RTP sender for testing the stream |

## Not yet adapted to V0.2

- Second WS2812 output: GPIO38 on V0.2, GPIO18 here.
- PIR inputs: logic inverted on V0.2 (transistor stage), and J8/J10 swapped to `PIR2`/`PIR1`.
- Audio: V0.2 adds the PCM5102A line out (`XSMT` on GPIO39) and drives the MAX98357A SD_MODE through a
  divider.

See `docs/12-v0-prototype.md`, "V0.2 board changes".
