# OpenHaunt Node firmware

Firmware for [OpenHaunt Node](https://github.com/OpenHaunt/node): a PoE-powered,
network-discoverable I/O node for scare mazes, haunted attractions and escape
rooms. One image for every node; the plug-on module — or, on a bare devkit, a
JSON file — decides what the node is.

The protocol is the one in OpenHaunt/node's
[06 — Firmware & Network](https://github.com/OpenHaunt/node/blob/docs/wire-payloads/docs/06-firmware-and-network.md):
mDNS `_openhaunt._tcp`, HTTP `/api/v1`, MQTT topics under `openhaunt/<sn>/`,
sACN for the DMX gateway. It is tested against
[the-pult](https://github.com/RuegerEvents/the-pult), which adopts and drives
nodes over exactly that.

## Boards

| `BOARD` | Hardware | Transport | Status |
|---|---|---|---|
| `oh-base` | The OpenHaunt base board: ESP32-WROOM-32, RMII Ethernet (RTL8201), W25Q128 | Ethernet, Wi-Fi fallback | compiles; no hardware yet |
| `esp32c6-devkitc-1` | ESP32-C6-DevKitC-1 | Wi-Fi | compiles; untested |
| `esp32-1732s019` | Sunton ESP32-1732S019 (ESP32-S3, 1.9" ST7789) | Wi-Fi | tested end to end |

Everything board-specific lives in `boards/<board>/sdkconfig.board` and the
Kconfig menu *OpenHaunt board* (`components/oh_board/Kconfig`): pins, transport,
status LED, display, the GPIOs presets may bind.

## Building

ESP-IDF v5.5 (installed with [eim](https://github.com/espressif/idf-im-ui) at
`~/.espressif/v5.5.3`; `scripts/env.sh` puts it in the shell).

```
scripts/build.sh esp32-1732s019                       # build
scripts/build.sh esp32-1732s019 -p /dev/cu.usbserial-110 flash monitor
scripts/build.sh oh-base menuconfig
scripts/build.sh esp32c6-devkitc-1 --fresh build      # after changing sdkconfig defaults
```

Each board gets its own `build/<board>/`. The pure-C core has host tests:

```
cmake -S test/host -B build/host && cmake --build build/host && ctest --test-dir build/host
```

## First boot

With no Wi-Fi credentials (or when the configured network cannot be reached
for 30 s) the node opens an access point **`openhaunt-<sn>`** and a captive
portal at `http://192.168.4.1/`. Enter the network there, or on the console:

```
oh wifi set <ssid> <passphrase>
```

Once on the network the node is `openhaunt-<sn>.local` with a web UI at `/`
(status, network, name and broker, module editor, firmware update) and the
`/api/v1` the controller uses.

## What the node is

The module is chosen at boot, in this order:

1. **The ID EEPROM** at I2C 0x50 (the base board's module connector), per doc 04.
   The module is held in reset until its descriptor is read, validated, and
   found to be within the power budget.
2. **A virtual module** stored in NVS: a JSON description in the same shape as
   the-pult's node-sim config, plus `bindings` that tie ports to drivers and pins.
   This is how a devkit poses as any module, and how a module nobody has built
   yet runs.
3. Nothing: headless, on the network, findable, reporting `module-absent`.

Catalogue presets (`input`, `contact`, `env`, `relay`, `led`, `oled`, `dmx`) are
one click in the web UI or `oh vmod preset <key>` on the console. A virtual
module with a real button and a real switch on a devkit:

```json
{ "name": "Bench node", "module": { "type": "0x0006", "name": "Dry Contacts" },
  "ports": [ { "port": 0, "name": "Contact 1", "access": "readwrite", "dataType": "boolean", "default": 0, "class": "switch" },
             { "port": 1, "name": "Button", "access": "readonly", "dataType": "boolean", "class": "contact" } ],
  "bindings": [ { "driver": "gpio_out", "port": 0, "gpio": 18 },
                { "driver": "gpio_in", "port": 1, "gpio": 9, "pull": "up", "invert": true } ] }
```

A port with no binding is a software port: it holds whatever it is given, which
is what the simulator does and what lets any preset run on a bare board.

### Drivers

| id | ports | binding keys |
|---|---|---|
| `gpio_in` | readonly boolean | `gpio`, `pull` (`up`/`down`/`none`), `invert`, `debounce_ms` |
| `gpio_out` | readwrite boolean | `gpio`, `invert`, `open_drain` |
| `ws2812` | color + number (intensity) | `gpio`, `count`, `order` |
| `mcp23017_in` | up to 16 readonly boolean | `addr`, `int_gpio`, `pins`, `invert`, `pullup` |
| `mcp23017_out` | up to 16 readwrite boolean | `addr`, `pins`, `invert` |
| `sht4x` | temperature + humidity (readonly number) | `addr`, `interval_ms`, `hyst_c`, `hyst_rh` |
| `ssd1306` | readwrite string | `addr`, `height` |
| `dmx_uart` | none; the universe is the interface | `uart`, `tx`, `rx`, `de`, `refresh_hz` |

Inputs are interrupt-driven and timestamped in the ISR; the timestamp on the
wire is unix milliseconds once SNTP has synced and milliseconds since boot before.

## Layout

```
components/
  oh_core      pure C, no IDF: port/module model, EEPROM descriptor, every wire
               shape (info/state/payloads/input/health), virtual-module parser,
               presets, debounce, E1.31 parser        <- host tests cover this
  oh_board     Kconfig board choice and the pin table
  oh_node      state store, driver registry, module bind, event fan-out, errors
  oh_drivers   the drivers above; oh_i2c the shared bus; oh_font Spleen 8x16
  oh_config    NVS: name, broker, dmx, Wi-Fi, IP; partial /config merge
  oh_net       Ethernet or Wi-Fi, setup AP + captive DNS, mDNS, SNTP
  oh_http      /api/v1 and the embedded web UI (www/index.html)
  oh_mqtt      status/input/health/output topics, offline will, event backlog
  oh_sacn      E1.31 receiver for a node that forwards a universe
  oh_ota       firmware update into the other slot, with rollback
  oh_status    status LED patterns and identify
  oh_display   ST7789 status screen (boards with one)
  oh_console   the `oh` REPL
  oh_module_eeprom  24AA02UID descriptor, MOD_nRESET, the catalogue table
  oh_app       ties it together
main/app_main.c   the boot sequence
```

## Console

`oh help` on the serial console (UART0, 115200). `scripts/oh-serial.py` drives it
without a terminal, and `scripts/oh-cmd.sh` sends lines to a running
`--daemon` session:

```
scripts/oh-serial.py /dev/cu.usbserial-110 --daemon build/serial &
scripts/oh-cmd.sh "oh info" "oh vmod preset input" "oh in 1 1"
```

`scripts/mqtt-sub.py` watches a broker with no dependencies, and
`scripts/pult-call.mjs` adopts or identifies a node in a running the-pult.

## Status

Verified on the ESP32-1732S019 against the-pult: setup AP and captive portal,
discovery and the TXT record, every `/api/v1` endpoint, adoption, MQTT in both
directions with the will, OTA with rollback. The base board's Ethernet, the ID
EEPROM path and the I2C drivers compile but have not met their hardware; the
DMX gateway listens for sACN and drives the UART but has not met a fixture.
