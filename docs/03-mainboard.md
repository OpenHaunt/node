# 03 — Mainboard

The mainboard is the same in every node. It owns the network connector, the magnetics, every
impedance-controlled trace in the design, the processor, and the two connectors that reach the power
board. It generates no power of its own and has no ground until a power board is
fitted.

## One build, no socketed phase

Earlier drafts of this document described two phases: a socketed WT32-ETH01 first, native silicon second.
**That is gone (D27).** The socket existed to postpone designing an ESP32 plus RMII PHY section, and under
D32 there is no RMII PHY at all — the W5500 is an SPI peripheral. Rev A carries the real parts.

| | |
|---|---|
| MCU | **ESP32-S3-WROOM-1**, PCB antenna, Wi-Fi and BLE present but unused |
| Ethernet | **W5500**, integrated MAC and PHY, SPI |
| IO board link | **USB full-speed**, from rev A |
| Network connector | etherCON, one of them |

## Network front end

### Connector

**Neutrik NE8FAH-LR-DAE.** etherCON A-series chassis receptacle, horizontal PCB mount, with the Halo light
ring.

| Property | Figure | Why it matters |
|---|---|---|
| PoE rating | **Type 4 Class 8, 100 W, per 802.3bt** | Well above what the magnetics allow, so the connector is never the limit |
| Current per contact | 1.5 A | Type 4 needs 480 mA per conductor, so 3× margin |
| Magnetics | **none** | See below, this is the consequence that matters |
| Shield | **isolated from panel ground** | The bonding decision moves onto the PCB where you can control it |
| Cutout | A-series, max 3 mm panel | Same cutout family as XLR, so a mixed panel stays tidy |
| IP rating | IP40 | Sealing is the enclosure's job, not the connector's |
| Dielectric strength | 1 kVdc | Connector-internal. The 1500 V barrier is the transformer's job |
| **Temperature** | **−30 to +80 °C** | **The lowest-rated part in the node.** See below |
| Lifetime | >1000 mating cycles | |

etherCON is the right call for this market. It locks, it survives being trodden on, and it is what the
entertainment industry already has in its cable stock. A standard RJ45 plug still mates with it, so nothing
is lost for a fixed install.

The shield being isolated from panel ground is worth more than it looks. Section *The enclosure trap* in
[02](02-system-architecture.md) is about cable shields accidentally shorting isolation domains through the
chassis. With this connector the shield arrives on a PCB pin and you decide, deliberately and in one
documented place, whether it bonds to chassis, bonds through an RC, or floats.

### Discrete magnetics

**The etherCON has no magnetics in it.** Neutrik does sell magnetics variants, `NE8FAH-M-Mb-LR-DAE` and
`NE8FAH-M-Gb-LR-DAE`, but their datasheets state twice over that they are **strictly not intended for use
in PoE systems**. They are not an option here. The magnetics have to be a separate part on the board.

This is a change from the two-board design, which assumed an integrated PoE magjack. It is mostly an
improvement: the transformer and the connector become independent choices, and the transformer can be
selected on its DC bias rating without also having to be the right mechanical part.

**Selected: Würth 7490220122**, from the WE-LAN 1000BASE-T with 4PPoE family.

| Property | Figure |
|---|---|
| Compliance | **"Compliant with IEEE 802.3bt for 4PPoE"**, stated on the datasheet |
| **DC bias** | **1000 mA per centre tap** — covers Type 4's 960 mA |
| Open-circuit inductance | **350 µH** min |
| Insulation test voltage | 1500 V RMS, 1 min |
| Data rate | 10/100/1000BASE-T, 4 channels, 1:1 |
| Temperature | −40 to +105 °C |
| Package | 24-pin SMD, 17.55 × 14.7 × 8.8 mm |
| Price / stock | $6.98, 18,748 at DigiKey |

**The bias rating applies to pins 13–24 only**, the X-side centre taps. That side faces the etherCON.
Fitting it the other way round puts PoE current through a winding not rated to carry it.

**Take the 350 µH seriously and do not substitute the 7490220123** to get its higher 1500 mA rating. That
part is 180 µH, a gigabit-optimised value that is thin for **10BASE-T**, which the W5500 can negotiate
down to. More DC headroom is worthless if the link degrades when a switch drops to 10 Mbit. See D28.

### Four channels, two of them unused

Pairs 1/2 and 3/6 carry data and use channels 1 and 2. **Pairs 4/5 and 7/8 bypass the transformer
entirely**: tie 4+5 together and 7+8 together and run them straight to J1. Channels 3 and 4 are left
unconnected.

**Do not route the spare pairs through channels 3 and 4.** It looks tidy and it is wrong. Centre-tap
injection works because the two conductors of a pair sit at the same DC potential and the current enters at
the tap, so the two winding halves carry equal and opposite MMF and cancel. The tied spare pairs are not
like that: wiring pin 4 to `MX3+` and pin 5 to `MX3−` puts the Mode B current straight through the winding
from one end to the other. That is a shorted turn carrying up to 960 mA. It saturates immediately and the
part overheats.

At 1000BASE-T all four pairs carry data and all four do use centre-tap injection, which is why the part has
four channels. On a 10/100 PHY two of them have no job.

The part is gigabit-capable, which buys nothing today because the ESP32's EMAC is 10/100 and will stay
10/100. It is simply how bt-rated magnetics are sold.

### Pin naming, and the two conventions

The Würth datasheet and the SamacSys symbol use different letters for the same pins. Both appear in this
project, so the mapping is worth having written down.

| Symbol | Datasheet | Pins | Side |
|---|---|---|---|
| `TCT1`, `TD1±` | `CD1`, `D1±` | 1, 2, 3 | **PHY** |
| `TCT2`, `TD2±` | `CD2`, `D2±` | 4, 5, 6 | PHY |
| `TCT3`, `TD3±` | `CD3`, `D3±` | 7, 8, 9 | PHY |
| `TCT4`, `TD4±` | `CD4`, `D4±` | 10, 11, 12 | PHY |
| `MCT1`, `MX1±` | `CX1`, `X1±` | 24, 23, 22 | **Cable** |
| `MCT2`, `MX2±` | `CX2`, `X2±` | 21, 20, 19 | Cable |
| `MCT3`, `MX3±` | `CX3`, `X3±` | 18, 17, 16 | Cable |
| `MCT4`, `MX4±` | `CX4`, `X4±` | 15, 14, 13 | Cable |

`T` is the transceiver side and `M` is the MDI side, which is the opposite of what "T for transformer"
would suggest. **`MCT1` and `MCT2` are the two taps that carry PoE current**, and they are the only ones
rated for it.

`TCT1` and `TCT2` are biased for the W5500 rather than left open. See *Magnetics on the PHY side* below.

### Where the power comes off

```
etherCON NE8FAH-LR-DAE
  │
  ├─ pair 1/2 ─▶ transformer ─┬─ centre tap ──▶ TCT  ┐
  │                           └─ data ──▶ PHY        │
  ├─ pair 3/6 ─▶ transformer ─┬─ centre tap ──▶ RCT  ├──▶ J1 ──▶ POWER BOARD
  │                           └─ data ──▶ PHY        │
  ├─ pair 4/5 ─────────────────────────────▶ SP45    │
  └─ pair 7/8 ─────────────────────────────▶ SP78    ┘
```

Four conductors, each carrying up to **960 mA** at Type 4. That figure catches people out, so it is worth
stating plainly: a pairset is two pairs, and the two conductors of the DC feed are the two pairs
themselves, so each tap carries the whole pairset current rather than a quarter of the total. Within pair
1/2 that 960 mA then splits into 480 mA per wire.

**Both the etherCON and the transformer clear Type 4**, at 1.5 A per contact and 1000 mA per centre tap
respectively, so nothing on the mainboard sets a power ceiling. What class a node runs at is decided by
the power board fitted underneath it. See D28.

Size J1's traces for 1 A each. See [06 — Board Interfaces](06-board-interfaces.md).

### Common-mode termination: not Bob Smith

**Classical Bob Smith termination cannot be used here, and trying would destroy the resistors.**

The textbook arrangement takes each pair through 75 Ω to one common node, and that node through a
1 nF / 2 kV capacitor to chassis. It terminates the roughly 75 Ω common-mode impedance of each pair so that
an unterminated pair does not behave as an antenna, radiating out and picking up in.

It assumes the four nodes sit at the same potential. On a 4-pair PoE link they do not: `PoE_TCT` against
`PoE_RCT` is up to **57 V**. Two 75 Ω resistors onto a shared node would draw 57 V / 150 Ω = **380 mA** and
dissipate about **21 W** in two chip resistors. There are no unused pairs on an 802.3bt link, so there is
nothing left that Bob Smith can legitimately terminate.

**What replaces it: one Y capacitor per net, no shared node, no resistors.**

```
PoE_TCT  ──[1nF/2kV]──┐
PoE_RCT  ──[1nF/2kV]──┤
PoE_SP45 ──[1nF/2kV]──┼──▶ CHASSIS
PoE_SP78 ──[1nF/2kV]──┘
```

Each branch has its own capacitor, so no DC path exists between the poles. This matters because the four
PoE nets run to the PD module's rectifier, whose output ground is isolated from chassis, which leaves
common-mode currents with nowhere to go. The capacitors give them a defined path.

**Fit the footprints, leave them unpopulated.** Same part as the chassis bond, `885342208024`, 1 nF / 2 kV,
1206. Whether they are needed is an EMC test result, and populating four capacitors afterwards is cheaper
than a respin.

75 Ω resistors are therefore **not** needed anywhere on this board.

### The light ring, and the LEDs it does not contain

**The connector has no LEDs in it.** The Halo ring is a light guide. Two LEDs sit on the mainboard, left
and right, and the ring carries their light to the front panel. Neutrik's drawing gives the pad layout as
Detail X at 5:1.

**They are addressable RGB.** Two **`IN-PI15TAT5R5G5B`**, cascaded on one data line:

| | |
|---|---|
| Size | 1.5 × 1.5 mm, **0.65 mm high** against Neutrik's 0.80 mm limit |
| Supply | 3.7 to 5.5 V, so **+5 V** |
| Data | single wire, WS2812 protocol, 800 kHz via the ESP32's RMT |

Both ring halves get full colour, which turns a bare presence indicator into a status display: green
running, red fault, blue for `/api/v1/identify`, amber during OTA. And it costs one GPIO rather than two.

**The data line needs a level shifter**, and the reason is worth stating because it catches people out. A
5 V WS2812-type LED has an input threshold of 0.7 × VDD = **3.5 V**, which a 3.3 V GPIO does not reliably
reach. Use a **`74AHCT1G125W5-7`**: the AHCT family has **TTL input thresholds** at 5 V supply, VIH 2.0 V.

**LVC, AHC and HC parts do not work here** — they have CMOS thresholds at 3.5 V. That includes the
`74LVC1G17` already in Part-DB.

```
ESP32 ──[220R]──▶ A │74AHCT1G125│ Y ──▶ LED1 DIN ─▶ DOUT ─▶ LED2 DIN
                     OE→GND, VCC=+5V      both +5 V, 100 nF each
```

`OE` is active low and must go to GND — left open, the output is high impedance and the ring stays dark.

### Footprint, from the Neutrik drawing

| Feature | Figure |
|---|---|
| Signal pins | 8, ⌀0.8 mm, two staggered rows, 2.54 mm pitch, 1.27 mm row offset |
| Shield pin | 1, **⌀1.2 mm**, marked GND on the drawing |
| Locating posts | 2, ⌀1.55 mm |
| Panel cutout | ≥⌀22 mm, A-series |
| Panel mounting holes | 19.8 ±0.1 mm apart, ⌀3.2 mm |
| Panel thickness | max 3 mm |

### Chassis and shield

The shield pin is the one to think about, and it is worth doing deliberately because the easy path gets it
wrong.

**`CHASSIS` is its own net, not an alias for `GND`.** The mounting holes are `MountingHole_Pad` and
therefore electrically live: putting them on `GND` bonds a metal enclosure to logic ground through four
screws, permanently, as a side effect of mechanical assembly. That is exactly the enclosure trap described
in [02](02-system-architecture.md), arrived at by accident rather than decision.

```
etherCON J10.SH ──▶ SHIELD ══▶ CHASSIS ──┬── H1..H4 mounting holes
                                         │
                                    ┌────┴────┐
                                  C 1nF     R 1M       ← fitted by default
                                  2 kV
                                    └────┬────┘
                                         │
                                    R0 0R │              ← footprint, DNP
                                         │
                                         ▼
                                        GND
```

**The shield goes straight to `CHASSIS`.** That is what it is for, and the NE8FAH-LR-DAE is isolated from
panel ground precisely so this is a PCB decision rather than a consequence of how the connector was bolted
in.

**The `CHASSIS` to `GND` bond is a population option.** Default is 1 nF / 2 kV in parallel with 1 MΩ. The
capacitor gives HF and ESD a path to ground, the resistor stops the enclosure floating up statically, and
together they pass no DC circulating current. If EMC testing shows a hard bond works better, fit the 0 Ω
and remove the other two. No respin either way.

That is the whole point: three footprints keep the question open until the test lab answers it, instead of
settling it now through four mounting holes. Parts are already in Part-DB: `885342208024` (1 nF / 2 kV,
1206), plus 1 MΩ and 0 Ω.



### Two things from the datasheet that constrain the design

**Maximum operating temperature is +80 °C, and the range is −30 to +80 °C.** That is tighter than anything
else on the board: the magnetics run to +105 °C and most of the silicon to +85 °C. The thermal analysis in
[02](02-system-architecture.md) puts a sealed af node at 65 to 75 °C internal, which leaves single-digit
margin, and a loaded bt node is worse. **The etherCON is now the lowest-rated part in the node** and should
be treated as the thermal limit rather than an afterthought.

**It does not intermate with Neutrik's CAT6 cable connectors**, specifically `NE8MC6-MO` and the `NKE6S*`
cables. Standard RJ45 plugs and Neutrik `NE8MX*` cable connectors are fine. In a venue where crews bring
their own etherCON cable stock, that is a support call waiting to happen and belongs in the product
documentation, not just here.

## Processor and ethernet

| Function | Part | Source | ~Price | Notes |
|---|---|---|---|---|
| MCU module | **ESP32-S3-WROOM-1-N4** | DigiKey | $5.21 | PCB antenna, 2176 in stock |
| Ethernet | **W5500** | DigiKey | $3.47 | MAC + PHY, SPI, 73,368 in stock |
| Network connector | **Neutrik NE8FAH-LR-DAE** | Part-DB | $8.66 | etherCON, Type 4 rated, Halo ring |
| LAN magnetics | **Würth 7490220122** | DigiKey | $6.98 | 802.3bt 4PPoE, 1000 mA/CT, 350 µH |
| Light ring LEDs | 0603, **≤0.80 mm**, Vf ≤2.6 V | Part-DB | $0.15 | ×2. See the light ring section |
| J1 PoE taps | **Würth 613004243121** | DigiKey | $0.91 | 2×2 right-angle socket |
| J2 | **Würth 61302021021** | Part-DB | — | 2×10 right-angle pin header. The socket is on the power board, see 06 |
| J4 | **Würth 613010243121** | Part-DB | — | 2×5 right-angle socket, to the IO board (D35) |
| 3.3 V regulator | **AMS1117-3.3** | Part-DB | — | 5 V → 3.3 V LDO, SOT-223, with 22 µF on the output (D36) |
| J21 console | Würth 61300511121 | Part-DB | $0.22 | 1×5 vertical |
| Level shifter | **74AHCT1G125W5-7** | DigiKey | $0.17 | SOT-753, TTL inputs at 5 V |
| Halo LEDs | **IN-PI15TAT5R5G5B** ×2 | Part-DB | $0.53 | Addressable RGB, 0.65 mm high |

**The RMII section is gone**, and with it the 50 MHz oscillator, its enable line, the GPIO0 strapping
problem and nine impedance-controlled traces. That was the most delicate part of the earlier design. The
W5500 needs a 25 MHz crystal and an SPI bus.

### What the W5500 costs

It is not a free swap and the docs should not pretend otherwise. ESP-IDF drives the W5500 in **MACRAW**
mode, so its hardware TCP/IP offload is unused, and practical throughput is **8 to 15 Mbit/s**. An Art-Net
console broadcasting 64 universes puts 13.4 Mbit/s on every node.

**sACN with IGMP snooping is therefore required**, not merely preferred, in any installation beyond a few
universes. It delivers only subscribed universes at 0.2 Mbit/s regardless of rig size. Art-Net broadcast
becomes a documented limitation. See D32 and [08 — Firmware & Network](08-firmware-and-network.md).

### USB pins are shared

On the ESP32-S3, **USB OTG and USB Serial/JTAG use the same GPIO19/20**. With OTG running as host for the
IO board, the S3 cannot also present a USB serial port, so it is flashed and debugged over UART through the
console header J21. Plan for that rather than discovering it at bring-up.

### Magnetics on the PHY side

The W5500 wants its centre taps biased, so `TCT1` and `TCT2` are **not** left open:

```
TCT1 ──[49.9 Ω]──▶ +3V3      TX centre tap, voltage mode
TCT2 ──[100 nF]──▶ GND       RX centre tap, current mode
```

This fixes channel 1 as transmit and channel 2 as receive, so etherCON pins 1/2 become the transmit pair
and 3/6 the receive pair. The two taps are biased differently and swapping them is a bring-up bug, so
verify against
[WIZnet's reference schematic](https://docs.wiznet.io/Product/Chip/Ethernet/W5500/ref-schematic).

49.9 Ω series resistors sit between the W5500's `TXP`/`TXN` and the transformer. **49.9 Ω is not in
Part-DB** and the value is not negotiable for ethernet termination.

## Pin allocation

The ESP32-S3-WROOM-1 has far more usable IO than the classic ESP32 did, and losing the RMII interface frees
nine more. The budget is no longer tight, so this table is a starting allocation rather than a closed
puzzle.

| Function | Signal | Notes |
|---|---|---|
| W5500 SPI | `ETH_SCK`, `ETH_MOSI`, `ETH_MISO`, `ETH_CS` | Any SPI-capable pins |
| W5500 control | `ETH_INT`, `ETH_nRST` | Interrupt is required for sane driver behaviour |
| IO board link | **`USB_DP`, `USB_DM` on GPIO19/20** | **Fixed by silicon.** Via J2 to the power board |
| IO board control | `IO_nRESET`, `IO_BOOTSEL`, `IO_ATTN` | |
| Power board | `I2C_SDA`, `I2C_SCL`, `PWR_ALERT`, `PWR_EN` | |
| Light ring | `HALO_DATA` | **One pin.** Two RGB LEDs cascaded via the level shifter |
| Console | `UART0_TX`, `UART0_RX` | **The only way to flash the S3** while OTG is host |

**GPIO19 and GPIO20 are not a choice.** The USB peripheral is hard-wired to them, so J2 pins 9 and 11
route to those two and nothing else can use them.

**Check the S3's own strapping pins before finalising**: GPIO0, GPIO3, GPIO45 and GPIO46. GPIO45 selects
the flash voltage and GPIO46 must be low at boot. They are a different set from the classic ESP32's, so the
rules from earlier revisions of this document do not carry over.

The Halo ring needs one pin rather than two, because the LEDs are cascaded on a single data line.

## Ethernet subsystem

```
etherCON ──▶ Würth 7490220122 ──▶ 49.9 Ω ──▶ W5500 ──SPI──▶ ESP32-S3
             MCT1/MCT2 ──▶ J1                 25 MHz xtal
```

No reference clock, no oscillator enable, no strapping interaction. The W5500 is an ordinary SPI peripheral
with an interrupt line, and ESP-IDF supports it natively via `esp_eth_mac_new_w5500()`.

**Keep the isolation boundary visible in the layout.** No traces, copper pour or ground fill crosses
between the transformer's cable side and its W5500 side, except the chassis coupling capacitors. That is
the 1500 V barrier IEEE 802.3 requires, and it is the same rule the PoE taps already impose.

## Flash

**No external flash chip.** The `ESP32-S3-WROOM-1-N16R2` carries 16 MB internally and that is what the
board uses.

Earlier revisions of this design specified a separate `W25Q128` on its own SPI bus, because writing to
internal flash stalls the XIP cache for 30 to 50 ms and the ESP32 was generating DMX timing where a frame
falls due every 22.6 ms. **With the RP2354 doing all timing (D19), nothing on the ESP32 is time-critical
any more.** A 50 ms stall delays network handling, which is buffered.

See D9 for the full reasoning and for when it should come back.

If the layout has room, keep a footprint. It costs nothing and it is the cheapest insurance against
discovering that recording volumes matter more than expected.

## Connectors

Three, and they are deliberately different from each other so that no two can be confused or cross-mated.

| Ref | To | Part | Pins | Carries |
|---|---|---|---|---|
| `J1` | Power board | **613004243121** | 2×2 | TCT, RCT, SP45, SP78. **Domain 1.** Keep-out on all sides |
| `J2` | Power board | **61302021021** | 2×10 | 5 V, GND, I2C, `PWR_ALERT`, `PWR_EN`, PSE type. Pin header; the socket is on the power board |
| `J4` | IO board | **613010243121** | 2×5 | USB, `IO_nRESET`, `IO_BOOTSEL`, `IO_ATTN`. Socket |
| `J10` | Network, outward | NE8FAH-LR-DAE | 8+SH | etherCON. Data pairs, PoE, shield, Halo ring |
| `J21` | Console | 61300511121 | 1×5 | `UART0_TX/RX`, `IO0`, `EN`, GND. **The only flashing route** |

**The mainboard has three board-to-board connectors.** Under D35 the IO board lies across the back of
mainboard and power board, so `J4` carries USB and the IO control lines straight to it and `J2` carries
only supply, metering and the two control lines. `J3` lives on the power board. An earlier version of this
section described D31's row, where everything travelled over `J2`; that is superseded.

**`J20` no longer exists.** The SWD passthrough was needed when the IO board sat in the middle of a stack;
with it at an edge, the SWD header belongs on the IO board itself, next to the RP2354.

### J2 pinout

```
Pin  Row A              Pin  Row B
 1   +5V                 2   +5V
 3   GND                 4   GND
 5   +5V                 6   GND
 7   GND                 8   GND
 9   TYP2_DET           10   GND
11   TYP3_DET           12   GND
13   GND                14   I2C_SDA
15   reserved           16   I2C_SCL
17   reserved           18   PWR_ALERT
19   reserved           20   PWR_EN
```

**USB and the IO control lines are on `J4` since D35**, not here. Pins 15 to 19 held them and stay
reserved. The authoritative pinout is in [06 — Board Interfaces](06-board-interfaces.md).

**One rail reaches the mainboard: 5 V, unconditional, on three contacts.** **24 V and 12 V never touch this
board** — they go straight from the power board to the IO board, which is the point of D31.

**The mainboard makes its own 3.3 V** (D36) with an **AMS1117-3.3** LDO from the 5 V feed. Load is
roughly 250 mA with Wi-Fi off, up to 500 mA while Wi-Fi transmits, so 0.4 to 0.85 W in a SOT-223: give the
tab a copper pour. An LDO rather than a buck deliberately, because this board carries every
impedance-controlled trace, the PHY and the magnetics, and it has no switching regulator on it. 22 µF with
some ESR on the output, as on the RP2354 base sheet; a lone small ceramic can make a 1117 oscillate.

Numbering follows the band scheme in [06 — Board Interfaces](06-board-interfaces.md): J1–J9 are
board-to-board and carry the same number on both boards, J10+ face outward, J20+ are service.

Full pinouts and the electrical contract are in [06 — Board Interfaces](06-board-interfaces.md).

## Layout constraints

**J1 and the isolation barrier.** The four tap nets and everything they touch are Domain 1. Maintain
isolation-grade clearance from every Domain 2 net, on both the mainboard and the power board, and keep the
barrier a straight, visible line on the board rather than a wandering one. No Domain 2 copper, including
ground pour, crosses it.

**The transformer sits between the etherCON and the barrier.** Its primary side is Domain 1, its secondary
Domain 2. It is the barrier, along with the Silvertel on the power board. Do not run a ground pour under it.

**The etherCON is 25 × 25 × 21.6 mm and panel-mounted.** It fixes one edge of the board and, with the
A-series cutout, one face of the enclosure. Its shield pin needs a deliberate destination, not a via to the
nearest ground.

**Differential pairs stay short and stay on the mainboard.** That is the whole reason the connector is on
this board. 100 Ω differential, matched within the pair, no stubs, and no split in the reference plane
underneath.

**Mind what sits across the J2 boundary.** Coplanar boards are edge to edge, so the power board's
switching corner can end up next to the ESP32 module and the USB pair. Keep that in mind when placing
both, and route the USB pair away from it.

**Mounting takes the mechanical load, not the connectors.** With the boards coplanar, any pull on the
network cable acts directly on J1 and J2 unless the screws take it first. Place the mainboard's mounting
holes so that they, and not the connectors, resist a tug on the etherCON.

**Check connector contact height before layout.** Coplanarity depends on the contact centreline sitting at
the same height above both PCBs. Sullins quote 3.15 mm for the `LGB` series; the Würth right-angle headers
need comparing against that from the drawing. If they differ, take both halves from one series.
