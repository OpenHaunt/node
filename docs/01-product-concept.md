# 01 — Product Concept

## The problem

A scare maze or escape room is a dense mesh of small effects. A typical room might need:

- a reed switch on a coffin lid
- a PIR that fires when someone crosses a threshold
- a button the players must press
- a DMX fixture that has to strobe on cue
- a strip of addressable LEDs behind a fake window
- mains power cut to a fog machine between shows
- an OLED showing a countdown as a prop

Each of these is trivial on its own. The pain is everything around them: a wall wart per device, a different
control protocol per vendor, no way to find devices on the network, and a rat's nest of cabling that has to
be re-pulled every time the room changes.

## The proposition

**One cable per effect.** Ethernet carries both the power and the data. Plug the node in, it appears on the
network, tells you what it is and what it can do, and you wire it into your show logic.

**One base board, many personalities.** The base board is always the same: PoE, network, processor, storage,
module connector. What the node *does* is determined entirely by the module clipped onto it. This means:

- one PCB design to maintain, certify and stock
- one firmware image that adapts to the attached module at runtime
- module development is cheap and low-risk — a new module is a small board, not a new product
- a failed effect is repaired by swapping a €8 module, not a €60 device

**Discoverable, not configured.** Nodes are found via mDNS, not by writing IP addresses on masking tape. The
node reports its module type, serial number and capabilities, so the control software can build the room map
itself.

## Target applications

### Scare mazes and haunted attractions

The dominant characteristics are: high effect density, seasonal installation and teardown, non-technical
operators, and an absolute requirement that the show keeps running. Nodes must survive being installed by
someone with a drill and a ladder in October and pulled out in November.

Consequences for the design:

- **PoE is the killer feature.** No mains spurs to run into a wooden maze structure, no wall warts to hide.
- **The device must fail visibly and safely.** An effect that silently stops is worse than one that flags itself.
- **Latency matters.** A scare that fires 300 ms late is not a scare. See
  [06 — Firmware & Network](06-firmware-and-network.md).

### Escape rooms

Lower effect density, but far longer uptime and much stricter reliability expectations — a room that fails
mid-game costs a booking and a refund. Escape rooms also tend to have *persistent* installations, which makes
maintainability and remote diagnostics more valuable than fast setup.

Consequences:

- **Remote status and health reporting** matter more than in a seasonal maze.
- **Deterministic reset behaviour** — the room must return to a known state between games, and after a power
  cycle.

### Adjacent uses

Small theatre, museum installations, themed retail, film and photo studio props. All of these have the same
shape: many small networked effects, no dedicated control infrastructure, and a strong preference for
something that just appears on the network.

## The two-part architecture

This is the central design decision of the project.

```
        ┌─────────────────────────────┐        ┌──────────────────────────┐
        │      BASE BOARD             │        │        MODULE            │
   ═══▶ │  PoE · Ethernet · ESP32     │ ◀────▶ │  Sensor or actuator      │
 RJ45   │  Flash · Module connector   │        │  ID EEPROM               │
        │  SELV only, always          │        │  Isolation if needed     │
        └─────────────────────────────┘        └──────────────────────────┘
              one design, one BOM,                   many designs,
              one certification                      each small and cheap
```

### Why split it

**Certification economics.** The base board is a networked SELV device: one EMC assessment, one radio module
approval inherited from the Wi-Fi module, one product file. A module that switches 400 V three-phase falls
under a completely different regulatory regime. Keeping them physically separate means the mains variant does
not drag the entire product line into the Low Voltage Directive. See
[07 — Compliance & Safety](07-compliance-and-safety.md).

**Board area.** A single board that could do everything would need the DMX isolation slot, the mains creepage
distances, the LED power stage and the connector variety simultaneously. It would be large, expensive, and
mostly unpopulated in any given installation.

**Development velocity.** A new module is a two-layer board with a connector, an EEPROM and the actual
circuit. It can be designed, ordered and tested in a fortnight. A new complete device cannot.

**Inventory.** Base boards are fungible. If a DMX effect becomes a light effect, you swap the module.

### What lives where

| On the base board | On modules |
|---|---|
| PoE extraction and isolation | All application-specific I/O |
| Ethernet PHY and MAC | All connectors facing the outside world (except RJ45) |
| Processor, RAM, flash | Any additional isolation barrier |
| Voltage rails (12 V / 5 V / 3.3 V) | Module identification EEPROM |
| mDNS, web UI, OTA, protocol stacks | Module-specific driver metadata |
| Status LEDs, config button | |

**DMX is a module, not a base function.** This was deliberated and settled: although the project started as a
DMX node, making DMX a module keeps the base board small and means nodes that only read a button do not carry
an isolated RS-485 transceiver and an XLR connector they will never use.

## Non-goals

Stating these explicitly so they don't creep back in:

- **Not a safety device.** Nothing commanded over a network is a safety function. An OpenHaunt Node must never
  be the thing that makes an installation safe for people to work on. See
  [07 — Compliance & Safety](07-compliance-and-safety.md).
- **Not a lighting console.** It executes cues, it does not author them.
- **Not battery or wireless powered.** PoE is the premise. Wi-Fi exists as a fallback and configuration path,
  not as the primary transport.
- **Not a pixel driver at scale.** A module can drive a reasonable run of WS2812s, but this is not a
  replacement for a dedicated pixel controller with a beefy power supply.
