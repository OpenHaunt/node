# 06 — Board Interfaces

The contracts between the three boards. This is the most important document in the repository: once boards
exist in the field, these interfaces cannot change without splitting the ecosystem.

Four connectors, deliberately different from one another so that no two can be confused or cross-mated.

| Ref | Between | Pins | Domain | Carries |
|---|---|---|---|---|
| `J1` | Mainboard → Power board | 2×2 | **1** | The four PoE taps, un-isolated |
| `J2` | Mainboard ↔ Power board | 2×10 | 2 | 3.3 V, 5 V, I2C, alert, enable, PSE type |
| `J3` | Power board → IO board | 2×10 | 2 | **Rails and ground only** |
| `J4` | Mainboard ↔ IO board | 2×5 | 2 | **USB and IO control, direct** |

**Mainboard and power board sit side by side; the IO board lies across the back of both** (D35). The
mainboard carries J1, J2 and J4; the power board carries J1, J2 and J3; the IO board carries J3 and J4.

Nothing passes through a board that has no business with it. The IO board's supply never crosses the
mainboard, and the mainboard's USB and control signals never cross the power board.

All connectors are Würth right-angle parts at 2.54 mm, 3 A, 250 V. **A 2×2 cannot enter a 2×5 or a 2×10, so
J1 is un-mateable with the others by geometry alone.** J3 and J4 are the same size and sit next to each
other, so they need a key — see Mechanics.

### The ground topology is a triangle, and that is new

Mainboard → power → IO → mainboard is a loop. D31's row was a chain with one return path; this is not.
`J4` carries the USB reference ground, `J3` carries the rail return, and an LED board's current will divide
between them according to impedance rather than intent.

**`J3` must win on copper.** Wide, short, low-impedance return on `J3`; the grounds on `J4` are signal
reference and nothing else. An IO board designer cannot infer this from the pinout, so it is stated here
and it is part of the contract.

## Reference designator scheme

Connector numbers are allocated in bands, so that a board-to-board connector carries **the same number on
both boards it joins**.

| Band | Purpose | Allocated |
|---|---|---|
| **J1–J9** | Board-to-board interfaces, the contract | `J1` PoE taps · `J2` mainboard to power · `J3` power to IO |
| **J10+** | Outward facing | `J10` etherCON |
| **J20+** | Service and debug | `J21` console UART on the mainboard · SWD header on the IO board |

The reason for the gaps: J1 to J3 are not board-local details, they are agreements between two PCBs. When
the power board calls its mating connector `J1` as well, this document can name one connector and mean both
ends of it. Anything specific to a single board sits above the band and can grow freely.

J4 to J9 are reserved for future board-to-board interfaces, for instance if a second IO slot is ever added
(D21).

## Design goals

1. **An IO board must never be able to prevent the mainboard from booting.** Third parties may design IO
   boards. This is now much easier to guarantee than it was, because J3 carries a serial link rather than
   raw GPIOs.
2. **The mainboard must identify both other boards without prior knowledge.** A power board through its
   EEPROM, an IO board through the link protocol.
3. **The isolation barrier must be visible.** J1 exists as a separate connector so that Domain 1 is
   something you can see on an assembled board, not a pin-numbering convention.
4. **Any isolation beyond SELV belongs to the IO board.**

---

## J1 — PoE taps

**4 pins. This connector carries un-isolated cable-side voltage and is the reason the isolation discussion
in [02](02-system-architecture.md) exists.**

| Pin | Signal | Source | Description |
|---:|---|---|---|
| 1 | `TCT` | pair 1/2 centre tap | Mode A, one polarity |
| 2 | `RCT` | pair 3/6 centre tap | Mode A, other polarity |
| 3 | `SP45` | pins 4 and 5, joined | Mode B, one polarity |
| 4 | `SP78` | pins 7 and 8, joined | Mode B, other polarity |

Polarity is not defined and does not need to be. The PD module's integrated bridge rectifiers resolve both
mode and polarity. What J1 must guarantee is that the right pair reaches the right module input.

### Electrical

| Property | Requirement |
|---|---|
| Current per pin | **960 mA** at 802.3bt Type 4 |
| Voltage, pin to pin | 57 V maximum |
| Voltage, to Domain 2 | 1500 V |
| Trace width | size for 1 A per conductor on both boards |
| Connector rating | Würth 61300411121 is 3 A, 250 V. Comfortable on both counts |

The 960 mA figure catches people out. A pairset is two pairs, and the two conductors of the DC feed are the
two pairs themselves, so each tap carries the whole pairset current rather than a quarter of the total.
Within pair 1/2 that current then splits into 480 mA per wire.

The mainboard transformer is rated **1000 mA per centre tap** and the etherCON 1.5 A per contact, so
neither caps this. The fitted power board decides the class. See D28.

### The two voltages, which are not the same requirement

This is easy to over-specify, so state it plainly.

**Pin to pin within J1: 57 V.** All four taps are cable-side conductors within ±57 V of each other. In a
hipot test they are tied together and driven as one node against Domain 2, so the potential between J1's
own pins during that test is zero. 2.54 mm clears 57 V by a wide margin.

**J1 to Domain 2: 1500 V.** That is a board clearance and creepage question, answered by the keep-out, not
by the connector's pitch.

### Mechanical and layout

**A keep-out on all sides, on both boards.** No Domain 2 copper, including ground pour, crosses into it.
The barrier should be a straight, visible line across each board rather than a wandering one, with a
silkscreen line marking it on both.

The reason J1 is a separate connector is not its internal spacing. It is that a separate connector makes
the barrier visible on an assembled board instead of a pin-numbering convention, and makes cross-mating
physically impossible.

---

## J2 — Power board to mainboard

**Würth 613020243121, 2×10, 2.54 mm, 3 A per contact.** Supply for the mainboard, the metering and
identification bus, two control lines, and the PSE type the power board detected.

| Pin | Row A | Pin | Row B |
|---:|---|---:|---|
| 1 | `+3V3` | 2 | `+3V3` |
| 3 | `GND` | 4 | `GND` |
| 5 | `+5V` | 6 | `GND` |
| 7 | `GND` | 8 | `GND` |
| 9 | `TYP2_DET` | 10 | `GND` |
| 11 | `TYP3_DET` | 12 | `GND` |
| 13 | `GND` | 14 | `I2C_SDA` |
| 15 | *reserved* | 16 | `I2C_SCL` |
| 17 | *reserved* | 18 | `PWR_ALERT` |
| 19 | *reserved* | 20 | `PWR_EN` |

**Only two rails reach the mainboard.** `+3V3` for the processor, ethernet and logic, roughly 350 mA, and
`+5V` for the two Halo LEDs and their level shifter, roughly 50 mA. 24 V and 12 V never touch the
mainboard — the power board sends them straight to the IO board over J3.

**`TYP2_DET` and `TYP3_DET` are new, and they are opto outputs.** The bt PD modules pull these pins low
from `VIN+`, so they sit in Domain 1 and reach Domain 2 through an optocoupler on the power board, which is
Silvertel's own reference arrangement. Two GPIOs plus pull-ups on the mainboard, and the node can tell
whether it is on an af, at or bt PSE and derate its budget accordingly. On an af power board both read
inactive, which is the correct answer.

**Pins 15, 17 and 19 held `IO_nRESET`, `IO_BOOTSEL` and `IO_ATTN`** until D35 moved them to J4. They stay
reserved rather than being reallocated: the connector is a contract, and a spare contact is worth more than
a tidier table. D34's `esp-hosted` SPI sketch is the obvious claimant.

**The USB pair has left this connector.** It is on J4 now, straight from the mainboard to the IO board, and
it no longer crosses a board carrying switching regulators. See D35.

### I2C address map

| Address | Device | Purpose |
|---|---|---|
| `0x40` | INA3221 | 12 V, 5 V, 3.3 V metering |
| `0x44` | INA226 | 24 V metering. Absent on boards with no 24 V rail |
| `0x50` | 24AA02UID | Descriptor and unique serial |

The mainboard is always the I2C master. Addresses above `0x50` are reserved for future power board use.

### Power contract

**Only 3.3 V is guaranteed.** Everything else depends on the fitted variant and is declared in the
descriptor. A mainboard that finds no 24 V rail is not in a fault state, it is on a power board built
from a 12 V module.

The rails share one budget and are not additive. An IO board drawing 500 mA at 5 V has consumed 2.7 W of
the total, not half of a separate 5 V allowance.

`PWR_EN` is deliberately not the 3.3 V rail's enable. The mainboard has to be alive to assert it, so 3.3 V
comes up unconditionally and the mainboard then enables the rest once it has read the descriptor and
checked the budget.

### Power board descriptor

At EEPROM address `0x00`, little-endian.

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0x00 | 4 | `magic` | ASCII `"OHP1"` for OpenHaunt Power, format version 1 |
| 0x04 | 1 | `descriptor_version` | Starts at 1 |
| 0x05 | 2 | `board_type` | Registered ID, see the table below |
| 0x07 | 1 | `hw_revision` | |
| 0x08 | 1 | `poe_standard` | 1 = af, 2 = at, 3 = bt Type 3, 4 = bt Type 4 |
| 0x09 | 1 | `declared_class` | The class signature presented to the PSE, 0 to 8 |
| 0x0A | 2 | `total_budget_mW` | Power available to the node after the PD module |
| 0x0C | 1 | `rails_present` | Bitfield: 0 = 3V3, 1 = 5V, 2 = 12V, 3 = 24V |
| 0x0D | 2 | `limit_mA_3v3` | eFuse trip threshold |
| 0x0F | 2 | `limit_mA_5v` | |
| 0x11 | 2 | `limit_mA_12v` | |
| 0x13 | 2 | `limit_mA_24v` | Zero if absent |
| 0x15 | 2 | `shunt_uohm_3v3` | Metering calibration |
| 0x17 | 2 | `shunt_uohm_5v` | |
| 0x19 | 2 | `shunt_uohm_12v` | |
| 0x1B | 2 | `shunt_uohm_24v` | |
| 0x1D | 16 | `name` | Null-padded ASCII, e.g. `"PWR-BT"` |
| 0x2D | 16 | `vendor` | Null-padded ASCII |
| 0x3D | 2 | `crc16` | Over 0x00 to 0x3C |
| 0xF8 | 8 | *(24AA02UID)* | Factory-programmed unique serial, read-only |

`total_budget_mW` is the honest figure after the PD module's own losses, not the standard's headline
number. For `PWR-AF` that is about 11,500 mW, not 12,950.

**On a bt board it is the maximum, not the current value.** The module declares a fixed class and the PSE
may be smaller than that class, so the real budget is only known once `TYP2_DET` and `TYP3_DET` have been
read. Firmware takes the lower of the descriptor figure and what the detect pins imply, and enforces that.
A `PWR-BT3` on an at switch is a 25.5 W node and must behave like one.

**These fields are the contract, not the topology.** A `PWR-BT` built on a 24 V module and one built on a
12 V module are different boards with different `rails_present`, and firmware handles both without knowing
which is which. That is deliberate: it lets a variant pick whatever PD module suits it.

| `board_type` | Variant | Module | Rails |
|---:|---|---|---|
| `0x0001` | `PWR-AF` | AG9912-MTB or AG9924-MTB | 3V3, 5V, 12V *or* 24V |
| `0x0002` | `PWR-AT` | AG5712/5724-LPB | 3V3, 5V, 12V *or* 24V |
| `0x0003` | `PWR-BT3` | AG59612/59624-LPB | 3V3, 5V, 12V *or* 24V |
| `0x0004` | `PWR-BT4` | AG59824-LPB | 3V3, 5V, 12V, 24V |

`0x0003` was `PWR-BT` before the range split into Type 3 and Type 4. Nothing has shipped, so it is
reassigned rather than deprecated — but once a board exists in the field this table is append-only.

---

## J3 — Power board to IO board

**Würth 613020243121 socket on the IO board, 61302021021 pin header on the power board. 2×10, 2.54 mm,
3 A per contact.** **Rails and ground, nothing else.**

| Pin | Row A | Pin | Row B |
|---:|---|---:|---|
| 1 | `+24V` | 2 | `GND` |
| 3 | `+12V` | 4 | `GND` |
| 5 | `+12V` | 6 | `GND` |
| 7 | `+5V` | 8 | `GND` |
| 9 | `+5V` | 10 | `GND` |
| 11 | `+5V` | 12 | `GND` |
| 13 | `+5V` | 14 | `GND` |
| 15 | `+3V3` | 16 | `GND` |
| 17 | `GND` | 18 | `GND` |
| 19 | `GND` | 20 | `GND` |

`+24V` and `+12V` may be absent — ask the descriptor before using them. `+3V3` is always present and is the
logic reference.

**This connector is sized for current, not pin count**, which is the point of D35, and the contact count
follows from a decision recorded in Q4: **each rail must be able to deliver nearly the whole budget on its
own**, not a fixed share of it. At 65 W that is 2.7 A on 24 V, 5 A on 12 V and 10 A on 5 V, so the low
rails need parallel contacts while 24 V needs one.

**Row B is ground throughout, and the twelve ground contacts are shared rather than assigned.** Only one
rail is ever near full load, so its return uses all of them. This is why the ground side is not sized per
rail: the constraint is the worst single rail, not the sum.

**3V3 is deliberately not in this scheme.** It is the logic supply, capped at 3 A. Full budget on 3.3 V
would be 18 A, which nobody draws through a 2.54 mm connector.

**The rails share one budget and are not additive.** An IO board drawing 500 mA at 5 V has consumed 2.7 W
of the total, not half of a separate 5 V allowance.

Keying: pin 1 marked on both boards, plus a mechanical key. J3 is 2×10 and J4 is 2×5, so they can no
longer be cross-mated by geometry — but they still sit next to each other on the same edge, so keep the
pin-1 marking unambiguous on both.

---

## J4 — Mainboard to IO board

**Würth 613010243121 socket on the mainboard, 61301021021 pin header on the IO board. 2×5, 2.54 mm.**
The whole logical interface, straight between the two boards that care about it.

| Pin | Row A | Pin | Row B |
|---:|---|---:|---|
| 1 | `GND` | 2 | `GND` |
| 3 | `USB_D−` | 4 | `GND` |
| 5 | `USB_D+` | 6 | `GND` |
| 7 | `IO_nRESET` | 8 | `IO_ATTN` |
| 9 | `IO_BOOTSEL` | 10 | `GND` |

**The USB pair is on 3 and 5**, adjacent in one row so both traces leave on the same side and stay
symmetric, with ground on 1 and 7 beside them and on 4 and 6 opposite.

**These grounds are signal reference only.** The rail return belongs on J3. See the triangle warning at the
top of this document.

**SWD is not here.** D31 put the SWD header on the IO board next to the RP2354, where it is directly
accessible because the IO board sits at an enclosure wall. D35 does not change that, and an earlier version
of this document contradicted D31 by routing `SWCLK`/`SWDIO` through to a mainboard header. **The IO board
carries its own SWD header.** See Q19 in [11 — Open Questions](11-open-questions.md).

### The link pins

Pins 3 and 5 carry **USB full-speed** from rev A. They are adjacent with a ground either side, routed as a
**90 Ω differential pair** on both boards.

**This is the interface that must not change.** The IO boards are the growing half of the catalogue, and a
generation 2 mainboard has to run generation 1 IO boards unmodified. That is why the link is USB now rather
than a UART with a migration path. See D20.

D35 improved its odds considerably. The pair no longer crosses the power board and two extra connectors, so
generation 2's concern — high-speed USB at 480 Mbit/s over that path — is largely answered in advance.

The speed matches on both ends and stays matched: the RP2354 is USB 1.1 full-speed, the ESP32-S3 host is
full-speed, and a future high-speed host reaches the same device through a hub.

**Series resistor footprints** on both boards, 27 Ω or 0 Ω depending on what the layout needs for impedance
matching. Decide at layout, not at schematic.

### Reset and boot

`IO_nRESET` carries a 10 kΩ pull-down on the mainboard, so the IO board is held in reset through the
mainboard's own boot. This maps to GPIO15 on the ESP32, which is an MTDO strapping pin, and the pull-down is
also the correct strapping state.

Side effect worth knowing during bring-up: GPIO15 low at boot suppresses the ROM boot log on UART0. That is
usually what you want in a product. Lift the resistor when you need the log.

`IO_BOOTSEL` is only useful once the link is USB. It is routed from rev A anyway, because a phase-2
mainboard then has the ROM bootloader available as a rescue path for a bricked IO board, and the alternative
is opening the enclosure with an SWD probe.

### IO board identification

**There is no EEPROM on an IO board.** The RP2354 describes itself over the link, which is one of the things
having an MCU on the IO board buys.

The mainboard sends `IDENTIFY` after releasing reset. The IO board replies with the same information the old
module EEPROM held, plus what it can now report that a passive board could not: firmware version, boot
reason, and self-test result.

| Field | Description |
|---|---|
| `magic` | `"OHI1"` for OpenHaunt IO, format version 1 |
| `descriptor_version` | Starts at 1 |
| `board_type` | Registered ID, see [07](07-io-board-catalog.md) |
| `hw_revision` | |
| `fw_version` | RP2354 application version |
| `serial` | 64-bit, from the RP2350 chip ID |
| `power_mA_3v3` | Declared worst case |
| `power_mA_5v` | |
| `power_mA_12v` | |
| `power_mA_24v` | |
| `rails_required` | Bitfield. The mainboard refuses to enable a board asking for an absent rail |
| `capability_flags` | See below |
| `name` | Human-readable ASCII |
| `vendor` | ASCII |
| `io_descriptor` | Variable length. The board's channel list, types and ranges |

`io_descriptor` is the part that did not exist before. A passive module could only say what it was, and the
mainboard had to already know what that meant. An IO board can describe its own channels, so a control
application can present a board type the firmware has never seen.

### Capability flags

| Bit | Meaning |
|---:|---|
| 0 | Has inputs |
| 1 | Has outputs |
| 2 | Supports timestamped events |
| 3 | Supports stored sequence playback locally |
| 4 | Has a failsafe state that survives link loss |
| 5 | Contains a mains-voltage section |
| 6 | Provides galvanic isolation on its outputs |
| 7 | Requires 24 V |

Bits 5 and 6 exist so that firmware and the web UI can present appropriate warnings, and so a node carrying
a mains board is visually distinguishable in the control software.

---

## Boot sequence

```
 1. 3.3 V comes up unconditionally from the power board
 2. Mainboard boots. IO_nRESET low, PWR_EN low, so the IO board is held down
 3. Read the power board descriptor at 0x50
    ├─ invalid or absent → no power board, or a broken one.
    │                      Run on 3.3 V, network up, report fault, stop here
    └─ valid → continue
 3a. Read TYP2_DET and TYP3_DET. Derate total_budget_mW to the PSE actually
     present, and report it if it is below the board's own rating
 4. Configure the metering from the descriptor's shunt constants
 5. Assert PWR_EN. Remaining rails come up in sequence
 6. Verify each declared rail is actually present and in tolerance
 7. Release IO_nRESET
 8. Send IDENTIFY, wait for the IO board descriptor
    ├─ no reply → IO board absent or dead. Re-assert reset, report fault
    └─ reply → continue
 9. Check rails_required against rails_present
    └─ missing rail → hold in reset, report a specific fault. Do not brown out
10. Check declared draw against total_budget_mW
    └─ exceeded → hold in reset, report over the network
11. Bind the driver for board_type, or fall back to the generic io_descriptor driver
12. Advertise over mDNS TXT records and the HTTP API
```

**A node with no IO board, or no power board, still joins the network and reports itself.** That is
deliberate. A node that has lost a board must be findable and diagnosable remotely, because the alternative
is a ladder.

Note that step 3 happens before step 5. The mainboard cannot enable the rails until it knows what they are,
which is why 3.3 V is unconditional and why strap pins were rejected as an identification scheme. There is
nothing useful to do in the window before I2C comes up.

## Mechanics

**The three boards sit coplanar, joined by right-angle connectors.** They are not stacked. **Mainboard and
power board sit side by side; the IO board lies across the back of both** (D35).

```
   ┌───────────────┬──────────────┐
   │   MAINBOARD   │ POWER BOARD  │
   │  ◀ etherCON   │   J1 · J2    │
   └───────┬───────┴──────┬───────┘
        J4 │           J3 │
   ┌───────┴──────────────┴───────┐
   │          IO BOARD            │
   └──────────────────────────────┘
        all three flat against the enclosure floor
```

**J3 and J4 go at the middle of the shared edge**, close to the seam between mainboard and power board.
Placed at the outer corners instead, every IO board would have to span the full width of both boards, which
is a great deal of empty copper on a reed-switch board. **This is a contract item, not a layout
preference** — it fixes how narrow an IO board is allowed to be, for the life of the ecosystem.

**The IO board must reach both connectors at once**, so their spacing is fixed on all three boards and one
of the three needs mounting-hole clearance to let the assembly settle. Same rule as before, one more
interface to satisfy.

### Why coplanar rather than stacked

**Thermal access.** Stacked, the hottest board is the power board and it ends up in the middle, wrapped by
the other two. Coplanar, every board lies flat and can couple straight into the enclosure floor. This is
the single biggest reason and it turns Q9 from the hardest open question into an ordinary design task.

**The boards no longer have to share an outline.** Stacking wanted one envelope and one stack height, which
is why D18 sized every power board for the largest PD module. Coplanar only requires the connector to sit
at the same position on the mating edge. A `PWR-AF` board may be small and a `PWR-BT4` board large.

**Everything is reachable during bring-up.** All three boards are probeable at once, with no need to
separate a stack to get at the middle one.

**Phase 1 stops being awkward.** A socketed WT32-ETH01 no longer sets the stack height for the whole
prototype.

### What it costs

**Footprint area.** Three boards side by side occupy more floor than three stacked. The enclosure becomes
flat and wide rather than compact and tall, which for a box screwed to a maze ceiling is arguably the
better shape anyway.

**The connector carries no mechanical load.** Standoffs used to hold a stack together. In the plane,
any pull on the network cable acts directly on the mating connector. **Both boards must be screwed to a
base plate or enclosure bosses**, positioned so the screws take the force and the connector does not. That
mounting is needed for the thermal path regardless, so the two requirements agree.

**Both halves come from Würth's WR-PHD range**, so contact heights are matched by design rather than by
comparing datasheets across manufacturers.

The naming is `6130` + contacts + rows + suffix, with `1021` for right-angle pin headers and `43121` for
right-angle sockets. The socket range has gaps the pin header range does not, which drives the sizes below.

**The mounting must not be rigid on both sides.** If both boards are pinned to exact
positions and the connector wants to sit 0.3 mm elsewhere, the joint is permanently stressed. Give one
board's mounting holes clearance — Ø3.4 mm rather than Ø3.2 for M3, or slots along the mating direction —
so the assembly settles itself as the screws go in.

### Connector pairs

| Interface | Layout | Socket | Pin header | Socket on |
|---|---|---|---|---|
| `J1` | **2×2** | `613004243121` | `61300421021` | Mainboard |
| `J2` | **2×10** | `613020243121` | `61302021021` | Mainboard |
| `J3` | **2×10** | `613020243121` | `61302021021` | IO board |
| `J4` | **2×5** | `613010243121` | `61301021021` | Mainboard |

**The right-angle socket range has gaps the pin header range does not.** There is no 2×8 and **no 2×7**
(`613014243121` does not exist), which is why J2 stays 2×10 with three reserved contacts rather than
shrinking to fit its nine signals. J1 is 2×2 rather than 1×4 for the same reason.

**J3 puts the socket on the IO board**, breaking the socket-on-mainboard rule deliberately: the power board
is the one carrying live rails, so the exposed pins belong on the board that is dead until `PWR_EN`. The
same reasoning that put sockets on the mainboard puts this one on the IO board.

Sourcing, checked 2026-09-07: `613010243121` 2288, `61301021021` 972, `613004243121` and `61300421021`
in stock. `613020243121` showed zero DigiKey stock on 2026-09-06 while the matching pin header had 1525 —
check other distributors before committing to J2.

**Right-angle footprints are not interchangeable between manufacturers.** Unlike vertical headers, the hole
positions relative to the body are vendor-specific, and Würth specifies **Ø1.1 ±0.20** against KiCad's
generic Ø1.0. Verify the generic footprints against the Würth drawing before layout.

The mainboard carries the sockets throughout, per D30's reasoning about which side stays live and, more
practically, so that a bare mainboard on the bench has no exposed pins to short with a probe.

`J21` on the mainboard and the SWD header on the IO board are board-local service connectors and stay
vertical.

Pogo pins were considered and rejected for this environment. Contact resistance under vibration at these
currents is a real concern, and corrosion in the humid, fog-fluid-laden air of a haunt is a genuine problem
that would not apply to most other products.

**The overall arrangement is not yet decided** — three boards in a row, or an L. That choice fixes which
edge carries J1 and J2 on the mainboard and therefore the board outline. See Q10 in
[11 — Open Questions](11-open-questions.md).

## Remote boards

Not everything can sit in the enclosure. Anything carrying mains voltage cannot: creepage, physical size,
enclosure and earthing requirements all forbid it.

**The answer is that a remote actuator gets its own node.** It costs a switch port and about $30, and it
sidesteps the cabled-bus problem entirely. This was the lean in the two-board design and the three-board
design makes it more obviously right, because an IO board is now a board with an MCU on it. A cable carrying
a UART between an MCU and its host, through a mains enclosure, is a worse idea than a second ethernet drop.
