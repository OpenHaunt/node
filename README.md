# OpenHaunt Node

**A PoE-powered, modular, network-discoverable I/O node for scare mazes, haunted attractions and escape rooms.**

One cable in. Power and data. Pick a power board for the load, pick an IO board for the effect.

---

## The idea in one paragraph

Immersive attractions are full of small, dumb jobs: flicker a ceiling light, trigger a DMX fixture, read a
reed switch on a prop door, light a strip of WS2812s behind a wall panel, cut mains to a fog machine. Today
each of those is solved with a different box, a different power supply and a different protocol. OpenHaunt
Node is a three-board set that gets **power and data from a single Ethernet cable (PoE)**. The
**mainboard** is always the same and does the network. The **power board** decides how much power the node
has, from 12 W to 71 W, and swapping it is how you change PoE class. The **IO board** carries its own
microcontroller and does the actual effect. Every node announces itself via mDNS, describes both boards it
is carrying, and speaks HTTP/JSON, MQTT and Art-Net/sACN, so you can drive the whole installation from
Node-RED on day one and from purpose-built software later.

## Status

**Concept and component selection complete. No schematic, no layout, no firmware yet.**

This repository is the captured state of a design conversation. Every major component has been selected and
justified, the pin budget has been verified to close for both build phases, and the known risks are written
down. It is meant to be picked up later without having to re-derive any of it.

The architecture was revised from two boards to three in September 2026, and the mainboard's processor
and ethernet were reworked in the same month once the IO board link became USB. Decisions D16 to D34 in
[10 — Design Decisions](docs/10-design-decisions.md) record what changed and why; earlier decisions that
were superseded say so at the top of their entry rather than being deleted.

## Documentation

| Document | Contents |
|---|---|
| [01 — Product Concept](docs/01-product-concept.md) | What it is, who it's for, why three boards |
| [02 — System Architecture](docs/02-system-architecture.md) | Block diagram, isolation domains, power tree, thermal |
| [03 — Mainboard](docs/03-mainboard.md) | etherCON, magnetics, ESP32, both build phases, pin allocation |
| [04 — Power Board](docs/04-power-board.md) | PoE variants, rails, eFuses, metering, identification |
| [05 — IO Board](docs/05-io-board.md) | Why the RP2354 is there, the link, firmware update |
| [06 — Board Interfaces](docs/06-board-interfaces.md) | The three connector contracts, descriptors, boot sequence, mechanics |
| [07 — IO Board Catalog](docs/07-io-board-catalog.md) | Planned IO boards, from DMX to mains switching |
| [08 — Firmware & Network](docs/08-firmware-and-network.md) | Two processors, discovery, APIs, protocols, Node-RED |
| [09 — Compliance & Safety](docs/09-compliance-and-safety.md) | EMC, SELV, radio approval, the mains problem |
| [10 — Design Decisions](docs/10-design-decisions.md) | Why each part was chosen and what was rejected |
| [11 — Open Questions](docs/11-open-questions.md) | What still has to be decided before layout |

**Start with 01 and 02.** If you are picking this up cold and want to know *why* something is the way it is,
go straight to [10 — Design Decisions](docs/10-design-decisions.md); it exists specifically so that settled
questions do not get re-opened.

## Headline specifications

| | |
|---|---|
| Power | IEEE 802.3af, 802.3at or 802.3bt Type 3/4, by power board. **12 W to 71 W** |
| Rails | Up to 24 V, 12 V, 5 V, 3.3 V, each with a hardware eFuse and V/I/W metering |
| Connector | Neutrik NE8FAH-LR-DAE etherCON with Halo light ring, discrete Würth 7490220122 |
| Network | 100 Mbit/s Ethernet, W5500 (MAC + PHY) over SPI |
| Host MCU | ESP32-S3, dual-core 240 MHz, USB host. Wi-Fi/BLE present, unused |
| IO MCU | RP2354, dual-core 150 MHz, 2 MB flash, 12 PIO state machines |
| Board link | **USB full-speed**, unchanged across generations |
| Storage | 4 MB internal (firmware + OTA) + 16 MB external (assets, recordings, IO images) |
| Discovery | mDNS, with both board types and the power budget in TXT records |
| Isolation | 1500 V (PoE), plus per-IO-board barriers where needed |

## Naming

**OpenHaunt** is the umbrella project, the high-level show-control software. **OpenHaunt Node** is the
hardware platform in this repository: one mainboard, a few power boards, and a growing family of IO boards.

## License

Not yet decided. See [11 — Open Questions](docs/11-open-questions.md).
