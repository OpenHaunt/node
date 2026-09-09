# 05 — IO Board

Every IO board carries an **RP2354** and talks to the mainboard over a link that is UART today and USB
later. This is the biggest change from the two-board design, where an IO board was passive silicon on a bus
and the ESP32 did all the work.

## Why the IO board has its own MCU

The old module connector carried raw ESP32 peripherals: I2C, a UART with a direction pin, one RMT-capable
fast line, an interrupt. That works, and it is how most add-on-board ecosystems do it, but it has three
properties I have come to dislike.

**It leaks the host's constraints into the contract.** Half of [06](06-board-interfaces.md) in the old
design was rules about ESP32 strapping pins: do not pull up pin 14, do not fight pin 16, GPIO12 is
deliberately absent. Those rules exist because the connector exposed GPIOs whose boot behaviour a third
party could break. A link that carries only a serial protocol cannot brick the host no matter what an IO
board does with it.

**One fast line is one fast line.** `MOD_FAST0` was a single RMT channel, which meant one WS2812 string, or
one bit-banged protocol, per node. The RP2354 has **12 PIO state machines**. WS2812, DMX, quadrature
encoders, servo timing and one-wire sensors all become PIO programs running concurrently, and none of them
compete with the ESP32's network stack for timing.

**Timing stops being shared.** On the old design, a 4 kB flash erase stalled the CPU for 30 to 50 ms while a
DMX frame needed servicing every 22.6 ms. That is why the external flash had to be on a separate SPI
controller. With the IO work on a second MCU the problem dissolves. The ESP32 can stall, garbage collect,
handle a burst of broadcast Art-Net, or reboot for OTA, and the outputs hold their state.

The cost is a second firmware image, a second build, a second update path, and a link protocol to specify.
That is real work, and it is written up honestly in [08 — Firmware & Network](08-firmware-and-network.md).

## The part

**RP2354A** for most boards, **RP2354B** where the pin count demands it.

| | RP2354A | RP2354B |
|---|---|---|
| Package | QFN-60, 7 × 7 mm | QFN-80, 10 × 10 mm |
| GPIO | 30 | 48 |
| Analogue inputs | 4 | 8 |
| Internal flash | **2 MB** | **2 MB** |
| Core | 2× Cortex-M33 at 150 MHz, or 2× Hazard3 RISC-V | same |
| SRAM | 520 kB | same |
| PIO | 12 state machines in 3 blocks | same |
| USB | 1.1 full speed, host or device | same |

**The flash is 2 MB, not 4.** An RP2354 is two stacked dice: the same RP2350 silicon as the flashless part,
plus a Winbond W25Q16JVWI. In every other respect, pinout included, it is identical to an RP2350A or
RP2350B, so a board can fall back to an external-flash RP2350 if 2 MB ever runs out.

Choosing the RP2354 over the RP2350 plus a discrete flash chip removes a part, a footprint, four routed
signals, and the QSPI layout constraints from every IO board in the catalogue. On boards this small that
matters more than the price difference.

### What it needs

- 5 V from `J3`, an LDO on the board for the 3.3 V IO supply (D36: there is no 3.3 V rail), and the
  1.1 V core from the RP2354's own regulator
- a 12 MHz crystal
- BOOTSEL reaching J3 so the mainboard can force USB bootloader mode
- **SWD on a local header.** The IO board sits at an edge under D31, so the probe reaches it directly;
  routing SWD back through the power board would be pointless

## The link

### USB full-speed, from the start

`USB_DP` and `USB_DM` on J3 pins 7 and 8. The mainboard is host, the IO board is device.

**This is deliberate and it cost the mainboard its internal ethernet MAC.** No shipping Espressif part has
both an internal MAC and a USB host, so the mainboard moved to an ESP32-S3 with a W5500 over SPI (D32). The
reason that trade was worth making is this interface: IO boards are the growing half of the catalogue, and
they have to run unmodified on the next mainboard. A UART now and USB later would have meant two
incompatible IO board families.

The RP2354 is USB 1.1 full-speed, and so is the S3 host, so nothing is lost. A generation 2 mainboard with
a high-speed host reaches the same full-speed device through a hub.

### Firmware update

The RP2350 ROM has a **USB bootloader**, and with a USB link the mainboard can use it directly: assert
`IO_BOOTSEL`, cycle `IO_nRESET`, and the RP2354 enumerates as a mass storage device that accepts a UF2
image.

**That makes the ROM the recovery path rather than a resident bootloader**, which removes what was the
highest-risk firmware in the project. A resident bootloader is still worth having for routine updates
without a reset dance, but it is no longer the only thing standing between a failed update and an SWD probe.

Bring the RP2354's USB out to a service connector as well. A laptop can then flash it directly, with no
mainboard involvement at all, which is the most robust recovery route the chip offers.

## Design rules for IO boards

Shorter than the old module rules, because most of the old ones were about not upsetting the ESP32's boot.

- **Declare your worst-case draw per rail, honestly.** The mainboard checks it against the fitted power
  board and refuses to release you from reset if it does not fit. Under-declaring means a brownout mid-show
  instead of a clear refusal at boot.
- **Do not assume a rail exists.** Only 5 V is guaranteed, and 3.3 V is yours to make from it. Whether 24 V exists depends on which PD module
  the fitted power board was built on, not on its class. Read the descriptor through the mainboard before
  enabling anything that needs it.
- **Do not assume you are the only thing drawing power.** The rails are a shared budget, not per-rail
  allowances.
- **`IO_ATTN` is open drain, active low.** Never drive it high.
- **Any isolation beyond SELV is yours.** The mainboard and the power board's output side are always SELV.
- **Reverse-mating must not destroy anything.** J3 is keyed, but design the 24 V and 12 V inputs so that a
  determined installer with a screwdriver does not turn a mistake into a fire.
- **Level shift on your own board.** All link and control signals are 3.3 V.

## What this makes easy

Worth recording, because several problems the two-board design had to work around simply stop existing.

**DMX becomes trivial.** A PIO state machine generates the break, mark-after-break and the 250 kbaud frame
directly, with no UART trickery and no dependence on ESP32 timing. RDM's turnaround timing, which is tight,
becomes a local problem on a chip doing nothing else.

**WS2812 stops being power-limited by the protocol side.** On `PWR-BT` the 5 V rail has about 49 W behind
it rather than the 10 W the af budget allowed, which is roughly 270 pixels at full white instead of 55. The
constraint moves from "one RMT channel and 10 W" to "how many amps do you want", and PIO can drive several
strings at once.

**Inputs get timestamped locally.** The RP2354 timestamps an edge on its own clock and reports it, so
jitter in the link and the network stack no longer accumulates into the reported event time. That is the
design rule from [08](08-firmware-and-network.md) finally being properly enforceable.

**A hung ESP32 no longer means dead outputs.** The IO board holds its last commanded state, and can be
given a defined failsafe to fall back to after a link timeout. On the old design a wedged ESP32 was a
wedged effect.
