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

**One mainboard, many personalities.** The mainboard is always the same: network, processor, storage, and
the connectors to the other two boards. How much power the node has is determined by the power board, and
what the node *does* is determined by the IO board. This means:

- one mainboard design to maintain, certify and stock
- one ESP32 firmware image that adapts to the fitted boards at runtime
- IO board development is cheap and low-risk, since a new IO board is a small board, not a new product
- PoE class becomes a stocking decision instead of a respin
- a failed effect is repaired by swapping a small board, not a whole device

**Discoverable, not configured.** Nodes are found via mDNS, not by writing IP addresses on masking tape. The
node reports its board types, serial numbers and capabilities, so the control software can build the room map
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
  [08 — Firmware & Network](08-firmware-and-network.md).

### Escape rooms

Lower effect density, but far longer uptime and much stricter reliability expectations — a room that fails
mid-game costs a booking and a refund. Escape rooms also tend to have *persistent* installations, which makes
maintainability and remote diagnostics more valuable than fast setup.

Consequences:

- **Remote status and health reporting** matter more than in a seasonal maze.
- **Deterministic reset behavior** — the room must return to a known state between games, and after a power
  cycle.

### Adjacent uses

Small theater, museum installations, themed retail, film and photo studio props. All of these have the same
shape: many small networked effects, no dedicated control infrastructure, and a strong preference for
something that just appears on the network.

## The three-board architecture

This is the central design decision of the project. It was two boards until the power and IO requirements
pulled apart far enough to justify a third.

```
   ┌──────────────────┐   ┌──────────────────┐   ┌──────────────────┐
   │  MAINBOARD       │   │  POWER BOARD     │   │  IO BOARD        │
═▶ │  etherCON        │──▶│  PoE tap         │   │  RP2354          │
   │  magnetics       │   │  Silvertel PD    │   │  application I/O │
   │  ESP32 · PHY     │◀──│  24 / 12 / 5     │   │  ID over link    │
   │  flash · network │   │  eFuse · metering│   │  isolation if    │
   │                  │──────── rails ──────────▶│  needed          │
   │                  │◀─────── link ───────────▶│                  │
   └──────────────────┘   └──────────────────┘   └──────────────────┘
     one design            a few variants          many designs
     one certification     af / at / bt            each small and cheap
```

### Why three and not one

**Certification economics.** The mainboard and the power board together are a networked SELV device: one
EMC assessment, one radio approval inherited from the Espressif module, one product file. An IO board that
switches 400 V three-phase falls under a completely different regime. Keeping them physically separate
means the mains variant does not drag the whole product line into the Low Voltage Directive.

**Board area.** One board that could do everything would need the DMX isolation slot, the mains creepage
distances, the LED power stage and the connector variety at once. It would be large, expensive, and mostly
unpopulated in any given installation.

**Development velocity.** A new IO board is a small board with a connector, an RP2354 and the actual
circuit. It can be designed, ordered and tested in a fortnight. A new complete device cannot.

**Field repair.** A failed effect is fixed by swapping a small board, not a whole device.

### Why the power board is separate

That is the part that is new, and it comes down to one observation: **PoE class is not a property of the
node, it is a property of the effect.** A reed switch needs 2 W. A pixel run needs 60 W. Those are the same
product doing different jobs, and in the two-board design they were different products, because moving from
802.3af to 802.3at was a base board respin. Different PD module footprint, different magjack rating,
different thermal design.

Splitting power out makes class a stocking decision. The same mainboard runs a 12 W af node and a 71 W bt
node.

The catch is that this only works if the parts you cannot swap are specified for the hardest case from the
start. The ethernet connector and its magnetics live on the mainboard, and they are what fix the range's
rated for 802.3bt Type 4, the top of the standard. Every node carries that front end, including the ones
that will never draw more than 3 W. A few dollars of insurance against a respin of the one board that must
not be respun, and it means PoE class stays entirely a power board decision.

### Why the IO board has its own processor

In the two-board design the connector carried raw ESP32 peripherals and the ESP32 did all the I/O work
itself. Giving each IO board an RP2354 buys three things.

The interface stops leaking the host's constraints. Most of the old interface document was rules about
ESP32 strapping pins, because a third-party board could pull the wrong line and prevent the node booting.
A link that carries a serial protocol cannot do that.

Timing stops being shared. The ESP32 can stall for 50 ms on a flash erase, absorb a burst of broadcast
Art-Net, or reboot for an OTA update, and the outputs hold their state throughout.

And the IO board gets 12 PIO state machines, which turns WS2812, DMX, servo timing and encoder reading from
things that compete for one timed output line into concurrent local programs.

The link is USB, and deliberately so: IO boards have to keep working on the next generation of mainboard
without redesign. See D20 and D33.

The cost is a second firmware image and a second update path. That is real work, not a free lunch.

### What lives where

| Mainboard | Power board | IO board |
|---|---|---|
| etherCON and all magnetics | PoE detection and classification | Application I/O of every kind |
| Every impedance-controlled trace | The isolation barrier | All outward-facing connectors except the etherCON |
| ESP32-S3, W5500, external flash | 24 V / 12 V / 5 V | Any additional isolation barrier |
| mDNS, web UI, OTA, protocol stacks | Per-rail eFuse and metering | Local timing and failsafe behavior |
| Status display on the Halo ring | Descriptor EEPROM | Self-description over the link |

**DMX is an IO board, not a mainboard function.** This was deliberated and settled early: although the
project started as a DMX node, making DMX an IO board keeps the mainboard small and means nodes that only
read a button do not carry an isolated RS-485 transceiver and an XLR connector they will never use.

## Beyond three boards

Two extensions are part of the concept but not of rev A. They are written down here so that rev A does
not close the door on them.

```
   ┌──────────┐ ┌───────────────┬──────────────┐
   │  SOUND   │ │   MAINBOARD   │ POWER BOARD  │
   │  BOARD   │─│  (or variant 2)   J1 · J2    │
   └──────────┘ └───────┬───────┴──────┬───────┘
                     J4 │           J3 │
                ┌───────┴──────────────┴───────┐
                │          IO BOARD            │
                └──────────────────────────────┘
```

### A sound board beside the mainboard

**A fourth coplanar board on the mainboard's free edge**, connected over I2S from the ESP32-S3 plus a
control bus, on a new board-to-board connector from the `J1` to `J9` band. It does one thing: turn a cue into audio, locally,
without a separate player and a contact-closure trigger.

- **Two variants.** Line level on a 3.5 mm mini-jack for installations with their own amplification, and
  a variant with a small amplifier for a passive speaker. The amplifier variant is bounded by what the
  mainboard can pass on: 5 V only, since 12 V and 24 V never touch the mainboard. A few watts of
  class D at 5 V fits; anything larger needs a rail path of its own and is a different design.
- **Storage travels with the sounds.** Whether that is an SD card on the sound board, eMMC on the sound
  board, or storage on the mainboard that the sound board streams from, is open. So is which side decodes.
  See Q20 in [11](11-open-questions.md).
- **The edge is not free yet.** In D35's arrangement the mainboard's outer edge carries the etherCON. Either
  the etherCON moves to the front edge or the sound board docks at the front; that is settled with the
  enclosure, not before.

### Mainboard variant 2: a Linux processor for video

**The same outline and the same three connectors, with a Raspberry Pi Zero 2 W or a Compute Module 4/5 in
place of the ESP32-S3**, for nodes that have to output video: projection mapping, a screen in a prop, a
display that is part of the effect. Sound can then live on the mainboard, since the processor already has
I2S and HDMI audio.

What it must keep: `J1`, `J2` and `J4` at the positions the contract fixes, the etherCON and its
magnetics, and the boot sequence in [06](06-board-interfaces.md). Power boards and IO boards then work
unchanged, which is the whole reason the interfaces are contracts. What it changes: the processor, the
firmware, the power budget of the mainboard itself, and the certification file. Concept only; no part is
selected.

## Non-goals

Stating these explicitly so they don't creep back in:

- **Not a safety device.** Nothing commanded over a network is a safety function. An OpenHaunt Node must never
  be the thing that makes an installation safe for people to work on. See
  [09 — Compliance & Safety](09-compliance-and-safety.md).
- **Not a lighting console.** It executes cues, it does not author them.
- **Not battery or wireless powered.** PoE is the premise. Wi-Fi exists as a fallback and configuration path,
  not as the primary transport.
- **Not a pixel driver at scale.** With a Type 4 power board an IO board can drive a genuinely useful run
  of WS2812s, roughly 350 pixels at full white against the 55 the af-only design allowed. It is still not a
  replacement for a dedicated pixel controller with its own supply, and long runs still want injection.
