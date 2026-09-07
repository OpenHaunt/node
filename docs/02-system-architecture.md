# 02 — System Architecture

## The three boards

```
╔═══ DOMAIN 1: cable side ══════════════════════════════════════════════════╗
║                                                                           ║
║   MAINBOARD                                 POWER BOARD                   ║
║  ┌──────────────────────┐                  ┌──────────────────────────┐   ║
║  │ etherCON NE8FAH      │   J1: 4 taps     │  Silvertel PD module     │   ║
║  │ + Würth 749022012x    │═════════════════▶│  MTB / LPB SMT family    │   ║
║  │  TCT RCT SP45 SP78   │   960 mA/pin     │                          │   ║
║  └──────────┬───────────┘                  └────────────┬─────────────┘   ║
║             │ data pairs                                │ 1500 V         ║
╚═════════════╬═══════════════════════════════════════════╬════════════════╝
              │                                           │
╔═════════════╬═══ DOMAIN 2: logic and rails (GND_A) ═════╬════════════════╗
║             ▼                                           ▼                ║
║   ┌──────────────────┐                     ┌──────────────────────────┐  ║
║   │ W5500  MAC+PHY   │                     │  24V ─ eFuse ─ INA226    │  ║
║   │ SPI              │                     │  12V ─ eFuse ─┐          │  ║
║   └────────┬─────────┘                     │   5V ─ eFuse ─┤ INA3221  │  ║
║            │ SPI                           │  3V3 ─ eFuse ─┘          │  ║
║   ┌────────▼─────────┐    J2: 3V3,5V,I2C   │  24AA02UID  0x50         │  ║
║   │ ESP32-S3         │◀═══════════════════▶│  + USB and IO control    │  ║
║   │ 16 MB int. flash │                     └──────────────────────────┘  ║
║   └────────┬─────────┘                                                   ║
║            │ J3: USB full-speed                                          ║
║   ┌────────▼─────────────────────────────────────────┐                   ║
║   │ IO BOARD                                         │                   ║
║   │   RP2354  ── PIO, ADC, GPIO ──▶ application I/O  │                   ║
║   └──────────────────────────────────┬───────────────┘                   ║
╚══════════════════════════════════════╬═══════════════════════════════════╝
                                       │
                          ╔════════════▼══════════════╗
                          ║ DOMAIN 3+ (per IO board)  ║
                          ║ DMX, mains, isolated I/O  ║
                          ╚═══════════════════════════╝
```

One mainboard, one power board, one IO board. Need a second universe or a second bank of inputs? Hang
another node next to it. That rule is unchanged from the two-board design and is the reason the link
between mainboard and IO board stays point-to-point.

### What each board owns

| Mainboard | Power board | IO board |
|---|---|---|
| RJ45 magjack and all magnetics | PoE detection, classification, isolation | RP2354 and its firmware |
| Every impedance-controlled trace | 24 V / 12 V / 5 V / 3.3 V generation | All application I/O |
| ESP32-S3, W5500, external flash | Per-rail eFuse and metering | All outward-facing connectors except RJ45 |
| Network stack, mDNS, OTA, web UI | Descriptor EEPROM | Any additional isolation barrier |
| Status LEDs, config button | Thermal management of the PD module | |

### Why the ethernet connector is on the mainboard

The magjack and the PHY have to sit close together on the same board, because the differential pairs
between them are the only impedance-controlled traces in the whole node. Putting the jack on the power
board would mean routing 100 Ω differential pairs across a board-to-board connector and then controlling
impedance on two PCBs instead of one.

Instead the mainboard keeps the jack and hands off four DC conductors. The power board becomes a cheap
two-layer board with no high-speed content at all.

The cost of that choice is written up in D17: the magnetics now live on the board you cannot swap, so they
set the ceiling for the whole range. **That ceiling is 802.3bt Type 4, 71.3 W at the PD**, fixed by the
the Würth 7490220122's 1000 mA DC bias rating, which matches the etherCON's own Type 4 Class 8 rating.
See D28.

## Isolation domains

### Domain 1 — cable side, spanning two boards

This is the part that changed. In the two-board design Domain 1 ended at the magjack and the PoE module,
both on one board. Now it crosses a connector.

Un-isolated PoE at 37 to 57 V leaves the mainboard on J1, travels across the power board, and ends at the
Silvertel module's input. Two barriers, on two different boards, both rated 1500 V:

- the Würth 7490220122 isolate the data pairs, on the mainboard
- the Silvertel transformer isolates the power, on the power board

Everything between them is Domain 1 and needs isolation-grade spacing from anything in Domain 2.

**J1 is a dedicated 4-pin connector with a keep-out around it, not four pins on the main connector.**

Worth being precise about what the keep-out is for, because it is easy to over-specify. All four taps sit
within ±57 V of each other, and in a hipot test they are tied together and driven against Domain 2. So
**J1's own pin-to-pin spacing only has to hold off 57 V**, which 2.54 mm does comfortably. The 1500 V
requirement is between J1 and Domain 2, and that is a board clearance question the keep-out answers.

The reason J1 is separate is not its internal spacing. It is that a separate connector makes the barrier
visible on the silkscreen instead of a pin-numbering convention, and makes it hard to mate the wrong pair
of boards. **J1 is a 1×4 and J2/J3 are 2×8, so they cannot cross-mate.** See
[06 — Board Interfaces](06-board-interfaces.md).

### Domain 2 — logic and rails (GND_A)

Everything downstream of the Silvertel's isolated output. All four rails, the metering, the ESP32, the PHY,
the flash, and the IO board's logic side. Always SELV.

GND_A originates on the power board and reaches the mainboard through J2. The mainboard has no ground of
its own until a power board is fitted.

### Domain 3+ — per IO board, optional

An IO board may add its own barrier. The DMX board's isolated transceiver gives 3750 V to the XLR side.
A mains board's barrier is the relay's own coil-to-contact insulation. Neither is the mainboard's problem.

**The rule that makes the architecture work is unchanged: the mainboard never sees anything but SELV, and
the power board's SELV side never sees anything but SELV.**

### The enclosure trap

With a metal enclosure, the RJ45 shield and an IO board's cable shield can both bond to chassis and short
two domains together, undoing barriers that exist on the PCBs. The chassis bonding scheme has to be a
documented decision per enclosure variant. On a loaded bt node the enclosure is also the heatsink, which
harder rather than easier. See [11 — Open Questions](11-open-questions.md).

## Power tree

```
PoE 37–57 V, 4-pair
   │
   ▼  J1 (4 conductors, up to 960 mA each)
   ▼
Silvertel PD module                          1500 V isolated, bridge rectifiers on board
   │
   ├── 24 V ──▶ eFuse ──▶ INA226  ──┐
   ├── 12 V ──▶ eFuse ──┐           │
   │                    ├─ INA3221 ─┤
   ├──▶ buck ── 5 V ──▶ eFuse ──────┤        J2
   │                                │        ══▶ MAINBOARD  (3V3 only)
   └──▶ buck ── 3V3 ──▶ eFuse ──────┘        ══▶ IO BOARD   (all four rails)
                                              ══▶ I2C: 0x40 INA3221
                                                       0x44 INA226
                                                       0x50 24AA02UID
```

Which rails exist depends on the power board fitted. Only 3.3 V is guaranteed. The descriptor says what
else is present and at what current limit, and firmware refuses to enable an IO board that asks for a rail
the fitted power board does not have.

### Power board variants

| Variant | PD module (24 V / 12 V) | Standard | PD budget | Module size | $ |
|---|---|---|---|---|---|
| `PWR-AF` | AG9924-MTB / AG9912-MTB | 802.3af | 12.95 W | 21 × 14 × 13 | 8.75 |
| `PWR-AT` | AG5724-LPB / AG5712-LPB | 802.3at | 25.5 W | 33 × 22 × 13 | 13.25 |
| `PWR-BT3` | AG59624-LPB / AG59612-LPB | 802.3bt Type 3 | 51 W | 40 × 25 × 15.8 | ~20 |
| `PWR-BT4` | AG59824-LPB / AG59812-LPB | 802.3bt Type 4 | **71.3 W** | 50.6 × 30 × 14.7 | 22.00 |

**Budget figures are the standard's guaranteed power at the PD, not the module's output rating.** Those
differ and it matters. The AG59824-LPB is a 100 W converter, but a compliant Class 8 link guarantees only
71.3 W at the PD. Budget on the standard, not the datasheet headline.

**Silvertel's SMT `MTB` and `LPB` families cover the whole range**, so every power board is machine-placeable
and none of them is the 56 mm through-hole SIL module the two-board design was built around. That question
is closed. Note the stock asymmetry on the bt parts: the 12 V variant has 288 in stock and the 24 V variant
has 8, which is a sourcing risk to watch rather than a design input.

**The module's output voltage is a per-variant implementation detail, not a range-wide standard.** Each
power board picks whatever module suits it, does its own down-conversion, and declares the resulting rails
and their per-rail budgets in its descriptor. The mainboard reads that and enforces it. What is contractual
is the descriptor, not the topology behind it.

**Power board outlines no longer have to match.** With the boards coplanar rather than stacked (D31), only
the position of J1 and J2 on the mating edge is fixed. A `PWR-AF` carrying a 21 × 14 mm module can be a
small board; `PWR-BT4` with a 50.6 × 30 mm module can be a large one. The enclosure accommodates the
largest variant and a smaller board simply leaves empty space.

**Ag5800 does not appear here and should not.** It is the 70 × 35 × 17 mm through-hole part, superseded by
the LPB family on size, mounting and efficiency. Ag6800 is the PSE module, the thing in the switch.

### Budget

At the af end, the arithmetic from the two-board design still holds, with one new entry:

| Consumer | Rail | Typical |
|---|---|---|
| ESP32-S3, ethernet active, Wi-Fi off | 3.3 V | ~100 mA |
| W5500 | 3.3 V | ~130 mA |
| Status LEDs, misc | 3.3 V | ~20 mA |
| RP2354 on the IO board, both cores at 150 MHz | 3.3 V | ~50 mA |
| **Mainboard plus bare IO board** | | **~1.5 W** |
| Conversion and eFuse losses | | ~0.4 W |
| **Available to IO board application circuits, `PWR-AF`** | | **~10 W** |
| **Available to IO board application circuits, `PWR-BT4`** | | **~65 W** |

The rails share one budget. They are not additive. An IO board drawing 500 mA at 5 V has consumed 2.7 W of
the total, not half of a separate 5 V allowance.

### Per-rail eFuse

Each rail leaves the power board through a current-limiting load switch with a hard trip that does not
depend on firmware. At 71 W a shorted IO board rail is a fire risk rather than a brownout, and the
Silvertel's own protection sits at the 48 V input, not at the 3.3 V rail that just shorted.

The eFuse also gives a clean way to power-cycle an IO board that has locked up, and a defined inrush ramp
so that hot-plugging a board with bulk capacitance does not collapse the rail for everything else.

**TPS259803ONRGER** is already in Part-DB and is a good fit for 12 V, 5 V and 3.3 V: 2.7 to 24 V, 8 A,
programmable current limit, circuit breaker, OVP, slew rate and UVLO, $2.64 with 7,916 in stock. Its 24 V
supply ceiling leaves no margin on a 24 V rail, so a board carrying one needs a higher-voltage part there,
something in the TPS1663 class at 60 V. See [04 — Power Board](04-power-board.md).

### Metering

Voltage, current and power per rail, over the I2C on J2.

The INA3221 is three channels in one package at one address, which covers 12 V, 5 V and 3.3 V. **It cannot
be used for the 24 V rail.** Its common-mode range tops out at 26 V, which leaves no margin on a 24 V rail
with switching ripple on it. The 24 V rail gets its own INA226 at 36 V instead.

The raw 48 V PoE input is deliberately not metered. It sits in Domain 1, so its I2C would have to cross the
isolation barrier, and the whole point of the architecture is that nothing crosses it. Input power is
inferred from the output rails and the module's rated efficiency.

## Thermal

**This section no longer describes one design.** An af node and a fully loaded bt node are different
thermal problems and possibly different enclosures.

| Source | `PWR-AF` | `PWR-BT` |
|---|---|---|
| PD module loss | ~1.5 W | **~3.8 W** at 93.1 % and full load |
| Rail regulators and eFuses | ~0.4 W | up to ~3 W |
| Mainboard electronics | ~1.3 W | ~1.3 W |
| IO board application circuits | 0 to 8 W | 0 to 42 W |
| **Total in the box** | **~3 to 11 W** | **~9 to 50 W** |

At the af end the old analysis stands: 65 to 75 °C internal in a sealed metal box at 40 to 50 °C ambient,
which is why the industrial-grade PHY was chosen and why every part's temperature grade needs checking
rather than assuming 85 °C is generous.

**Efficiency changes where the heat lands, not how much of it there is.** This is worth stating plainly
because it is easy to talk yourself out of the problem. Whatever the node draws from the cable ends up as
heat in the enclosure, minus the small fraction that leaves as light or motion. A fully loaded `PWR-BT4`
node pulls 71.3 W through a sealed box, and no converter efficiency changes that total.

What the LPB family's 93.8 % buys is the *split*. An earlier draft assumed the 89 %-efficient Ag5800 and
put 10.5 W into a DIL module standing proud of the power board, which is the worst possible place for it.
The AG59824-LPB puts 4.4 W into a surface-mount part lying flat. The heat is now mostly on the IO board,
spread across its output devices, which is a far easier thermal shape than a single hot module.

**The coplanar arrangement helps here.** With the three boards edge to edge rather than stacked, every one
of them lies flat and can couple directly into the enclosure floor. Stacked, the power board would have
been the hottest and would have sat in the middle, insulated by the other two. See D31.

A fully loaded bt node still needs a deliberate thermal path, and it interacts with the chassis bonding
question above, because a thermal path is usually also an electrical one. See
[11 — Open Questions](11-open-questions.md).

## Network architecture

```
        ┌──────────────┐
        │ PoE switch   │──── Node (DMX IO board)      ──▶ XLR ──▶ fixtures
        │ + IGMP       │──── Node (relay IO board)    ──▶ mains ──▶ fog machine
        │   snooping   │──── Node (input IO board)    ◀── reed switch, PIR, button
        └──────┬───────┘──── Node (WS2812 IO board)   ──▶ LED strip
               │
        ┌──────▼──────────────────────┐
        │ Node-RED / OpenHaunt server │  mDNS discovery, HTTP/JSON, MQTT
        │ (a mini PC or a Pi)         │  Art-Net / sACN source for DMX nodes
        └─────────────────────────────┘
```

No daisy-chaining, no bus. Every node is a switch port. That costs ports and buys independence: a failed
node cannot take down its neighbours, and troubleshooting is per-device.

Mixing power classes on one switch needs thought that the af-only design did not. A switch that budgets
90 W for a bt node has that much less for everything else, and PoE budget exhaustion usually shows up as a
port that silently refuses to power up. The node reports its own class and measured draw over MQTT, which
at least makes the arithmetic visible. See [08 — Firmware & Network](08-firmware-and-network.md).

**Use a switch with IGMP snooping. This is a requirement, not a recommendation.** The W5500 over SPI
manages a practical 8 to 15 Mbit/s, and an Art-Net console broadcasting 64 universes puts 13.4 Mbit/s on
every node. sACN with a correct IGMP join keeps that at 0.2 Mbit/s regardless of rig size. Art-Net
broadcast in a large rig is a documented limitation of this hardware generation. See D32.
