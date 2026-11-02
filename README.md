<div align="center">

# OpenHaunt Node

**A PoE-powered, modular, network-discoverable I/O node for scare mazes, haunted attractions and escape rooms.**

One cable in. Power and data. Pick a power board for the load, pick an IO board for the effect.

Driven by [**the-pult**](https://github.com/RuegerEvents/the-pult), or by anything else. The protocol is
[open and documented](docs/08-firmware-and-network.md), plain HTTP/JSON, MQTT and sACN.

[![Status](https://img.shields.io/badge/status-prototypes%20validated%20%26%20tested-brightgreen)](docs/12-v0-prototype.md)
[![Firmware](https://img.shields.io/badge/firmware-0.5.0-blue)](firmware/)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-5.5-E7352C?logo=espressif&logoColor=white)](firmware/)
[![KiCad](https://img.shields.io/badge/KiCad-10.0-314CB0?logo=kicad&logoColor=white)](hardware/)
[![PoE](https://img.shields.io/badge/PoE-802.3af%20%7C%20at%20%7C%20bt-orange)](docs/04-power-board.md)
[![License](https://img.shields.io/badge/license-not%20yet%20decided-lightgrey)](docs/11-open-questions.md)

</div>

---

## The idea

Immersive attractions are full of small, dumb jobs: flicker a ceiling light, trigger a DMX fixture, read a
reed switch on a prop door, light a strip of WS2812s behind a wall panel, cut mains to a fog machine. Today
each of those is solved with a different box, a different power supply and a different protocol.

OpenHaunt Node is a three-board set that gets **power and data from a single Ethernet cable (PoE)**:

| Board | Role |
|---|---|
| **Mainboard** | Always the same. Does the network. |
| **Power board** | Sets the node's power, 12 W to 71 W. Swapping it is how you change PoE class. |
| **IO board** | Carries its own microcontroller and does the actual effect. |

Every node announces itself over mDNS, describes both boards it carries, and speaks HTTP/JSON, MQTT and
Art-Net/sACN. [the-pult](https://github.com/RuegerEvents/the-pult) is the show console built for it: it
discovers, adopts and drives nodes. The protocol is not tied to it, though. It is open and documented, so
Node-RED, QLab, a Python script or any other show-control software can drive a node directly.

## Status

**Prototypes validated and tested.**

Before building the three-board family, a single-board prototype proves the processor, the network, PoE
and the firmware ([D37](docs/10-design-decisions.md)). That prototype, **V0**, carries the mainboard
electronics, an 802.3af PoE module and the IO the first haunt needed: four relays, two PIR inputs, two
WS2812 outputs and audio.

| | |
|---|---|
| **V0.1** | Ordered at JLCPCB on 2026-10-05, brought up and tested ([`test/firmware`](test/firmware/)) |
| **V0.2** | Bring-up fixes and line out. Tested on the bench against [the-pult](https://github.com/RuegerEvents/the-pult) |
| **Firmware** | 0.5.0: discovery, HTTP and MQTT APIs, effects, audio, pixel strips, broker failover |
| **Product boards** | Mainboard, `PWR-BT4` and `IO-LED8` have schematics and layouts in progress |

Decisions D1 to D38 in [10 — Design Decisions](docs/10-design-decisions.md) record what was chosen and why.
Superseded decisions say so at the top of their entry instead of being deleted.

## Headline specifications

| | |
|---|---|
| ⚡ **Power** | IEEE 802.3af, 802.3at or 802.3bt Type 3/4, by power board. **12 W to 71 W** |
| 🔌 **Rails** | Up to 24 V, 12 V and 5 V, each with a hardware eFuse and V/I/W metering. Every board makes its own 3.3 V |
| 🔗 **Connector** | Neutrik NE8FAH-LR-DAE etherCON with Halo light ring, discrete Würth 7490220122 magnetics |
| 🌐 **Network** | 100 Mbit/s Ethernet, W5500 (MAC + PHY) over SPI |
| 🧠 **Host MCU** | ESP32-S3, dual-core 240 MHz, USB host. Wi-Fi stands by as a fallback on V0 |
| 🎛️ **IO MCU** | RP2354, dual-core 150 MHz, 2 MB flash, 12 PIO state machines |
| 🔀 **Board link** | **USB full-speed**, unchanged across generations |
| 💾 **Storage** | 16 MB internal flash (firmware, OTA, assets, IO images) |
| 📡 **Discovery** | mDNS `_openhaunt._tcp`, with both board types and the power budget in TXT records |
| 🛡️ **Isolation** | 1500 V (PoE), plus per-IO-board barriers where needed |

## Repository layout

```
docs/       Concept, architecture, per-board design, protocols, decisions
firmware/   ESP-IDF firmware for every node: one image, the module decides what it is
hardware/   KiCad projects
  OH_Node_V0_Prototype/   single-board prototype, production files, 3D-printed enclosure
  OH_Node_Mainboard/      product mainboard
  OH_Node_PWR_BT4/        802.3bt Type 4 power board
  OH_Node_IO_LED8/        first IO board, RP2354
  tools/                  gnd_reach, return_vias: board analysis scripts
test/       Bring-up firmware for V0.1 (PlatformIO)
```

## Documentation

**Start with 01 and 02.** If you want to know *why* something is the way it is, go straight to
[10 — Design Decisions](docs/10-design-decisions.md). It exists so that settled questions are not reopened.

#### Hardware

| Document | Contents |
|---|---|
| [01 — Product Concept](docs/01-product-concept.md) | What it is, who it's for, why three boards |
| [02 — System Architecture](docs/02-system-architecture.md) | Block diagram, isolation domains, power tree, thermal |
| [03 — Mainboard](docs/03-mainboard.md) | etherCON, magnetics, ESP32-S3, W5500, pin allocation |
| [04 — Power Board](docs/04-power-board.md) | PoE variants, rails, eFuses, metering, identification |
| [05 — IO Board](docs/05-io-board.md) | Why the RP2354 is there, the link, firmware update |
| [06 — Board Interfaces](docs/06-board-interfaces.md) | The three connector contracts, descriptors, boot sequence, mechanics |
| [07 — IO Board Catalog](docs/07-io-board-catalog.md) | Planned IO boards, from DMX to mains switching |
| [09 — Compliance & Safety](docs/09-compliance-and-safety.md) | EMC, SELV, radio approval, the mains problem |
| [12 — V0 Prototype](docs/12-v0-prototype.md) | The single-board prototype as built, and its revisions |

#### Firmware and protocol

| Document | Contents |
|---|---|
| [08 — Firmware & Network](docs/08-firmware-and-network.md) | Two processors, discovery, APIs, protocols, Node-RED |
| [13 — Audio](docs/13-audio.md) | Clips on the node, RTP streams, sound at a stated show time |
| [14 — Pixels](docs/14-pixels.md) | WS2812 strips: effects drawn on the node, or streamed pixels |
| [15 — Brokers](docs/15-brokers.md) | MQTT brokers, and a console that fails over |
| [16 — Standalone Show](docs/16-standalone-show.md) | A show a console puts on a node, which then runs it alone (designed, not built) |

#### Project

| Document | Contents |
|---|---|
| [10 — Design Decisions](docs/10-design-decisions.md) | Why each part was chosen and what was rejected |
| [11 — Open Questions](docs/11-open-questions.md) | What still has to be decided |

## Naming

**OpenHaunt** is the umbrella project. **OpenHaunt Node** is the hardware platform in this repository:
one mainboard, a few power boards, and a growing family of IO boards. Show control is
[the-pult](https://github.com/RuegerEvents/the-pult), which speaks the node protocol of
[08 — Firmware & Network](docs/08-firmware-and-network.md).

## License

Not yet decided. See [11 — Open Questions](docs/11-open-questions.md).
