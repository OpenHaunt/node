# OpenHaunt Node

**A PoE-powered, modular, network-discoverable I/O node for scare mazes, haunted attractions and escape rooms.**

One cable in. Power and data. Snap on whatever the effect needs.

---

## The idea in one paragraph

Immersive attractions are full of small, dumb jobs: flicker a ceiling light, trigger a DMX fixture, read a
reed switch on a prop door, light a strip of WS2812s behind a wall panel, cut mains to a fog machine. Today
each of those is solved with a different box, a different power supply and a different protocol. OpenHaunt
Node is one base board that gets **power and data from a single Ethernet cable (PoE)**, and a family of
**plug-on modules** that give it whatever I/O the effect needs. Every node announces itself on the network
via mDNS, describes the module it's carrying, and speaks HTTP/JSON, MQTT and Art-Net/sACN — so you can drive
the whole installation from Node-RED on day one and from purpose-built software later.

## Status

**Concept and component selection complete. No schematic, no layout, no firmware yet.**

This repository is the captured state of a design conversation. Every major component has been selected and
justified, the pin budget has been verified to close, and the known risks are written down. It is meant to be
picked up later without having to re-derive any of it.

## Documentation

| Document | Contents |
|---|---|
| [01 — Product Concept](docs/01-product-concept.md) | What it is, who it's for, why two-part |
| [02 — System Architecture](docs/02-system-architecture.md) | Block diagram, isolation domains, power tree |
| [03 — Base Board](docs/03-base-board.md) | Full BOM with part numbers, complete pin allocation |
| [04 — Module Interface](docs/04-module-interface.md) | The connector contract, module ID EEPROM, mechanics |
| [05 — Module Catalog](docs/05-module-catalog.md) | Planned modules, from DMX to mains switching |
| [06 — Firmware & Network](docs/06-firmware-and-network.md) | Discovery, APIs, protocols, Node-RED integration |
| [07 — Compliance & Safety](docs/07-compliance-and-safety.md) | EMC, SELV, radio approval, the mains problem |
| [08 — Design Decisions](docs/08-design-decisions.md) | Why each part was chosen and what was rejected |
| [09 — Open Questions](docs/09-open-questions.md) | What still has to be decided before layout |

**Start with 01 and 02.** If you are picking this up cold and want to know *why* something is the way it is,
go straight to [08 — Design Decisions](docs/08-design-decisions.md); it exists specifically so that settled
questions do not get re-opened.

## Headline specifications

| | |
|---|---|
| Power | IEEE 802.3af PoE, ~12 W at the PD |
| Rails | 12 V (~850 mA to modules), 5 V, 3.3 V |
| Network | 100 Mbit/s Ethernet, internal MAC + RMII PHY |
| MCU | ESP32 (classic), dual-core 240 MHz, Wi-Fi as fallback |
| Storage | 4 MB internal (firmware + OTA) + 16 MB external (assets, recordings) |
| Discovery | mDNS, with module type advertised in TXT records |
| Module bus | 12 V / 5 V / 3.3 V, I²C, UART, one RMT-capable fast line, interrupt |
| Isolation | 1500 V (PoE), plus per-module barriers where needed |

## Naming

**OpenHaunt** is the umbrella project — the high-level show-control software. **OpenHaunt Node** is the
hardware platform in this repository: one base board plus a growing family of modules.

## License

Not yet decided. See [09 — Open Questions](docs/09-open-questions.md).
