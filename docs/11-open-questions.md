# 11 — Open Questions

Everything that must be resolved before a schematic is worth drawing, roughly in the order it blocks
progress.

Several questions from the two-board design are now answered and have been removed: the PoE module form
factor, the module connection mechanics, and the af-versus-at decision were all settled by the three-board
split and by moving to Silvertel's SMT families. Q1, Q2, Q3, Q5, Q6, Q7, Q8 and Q10 have since been
answered and are kept in place, marked RESOLVED or CLOSED, so their numbering stays stable.

---

## Blocking — decide before schematic

### Q1 — LAN magnetics — RESOLVED

**Würth 7490220122.** WE-LAN 1000BASE-T with 4PPoE, explicitly *"Compliant with IEEE 802.3bt for 4PPoE"*,
**1000 mA per centre tap**, **350 µH**, 1500 V RMS, −40 to +105 °C, 24-pin SMD 17.55 × 14.7 × 8.8 mm,
$6.98, 18,748 in stock. Covers Type 4's 960 mA, so the mainboard sets no power ceiling. See D28.

**Two layout constraints it brings**, both settled. The bias rating applies to **pins 13–24 only** (`MCTx`,
`MXx±`), so that side faces the etherCON. And pairs 4/5 and 7/8 **bypass the transformer** — tie 4+5 and
7+8 straight to J1, leaving channels 3 and 4 unconnected. Routing the tied spare pairs across a winding
would put ~960 mA through it as a shorted turn.

**Do not substitute the 7490220123** for its higher 1500 mA rating. It is 180 µH, thin for 10BASE-T
fallback.

### Q2 — Wi-Fi — RESOLVED

**No Wi-Fi.** The node is PoE-powered and therefore always has a cable. Configuration goes through the web
UI, discovery through mDNS.

The ESP32-S3 chosen in D32 has Wi-Fi and BLE on board anyway. They stay unused, cost nothing extra, and the
module approval covers them. **BLE remains available** if a maintenance channel is ever wanted — being able
to configure a node four metres up without a laptop is exactly what Wi-Fi was supposed to provide, without
the show-network traffic.

Where wireless does belong is a battery or DC-powered power board. See D34.

### Q3 — USB host — RESOLVED for this generation

**ESP32-S3 with a W5500 now, ESP32-S31 for generation 2.**

The requirement that settled it: IO boards must run unmodified on the next mainboard, so the link is USB
from rev A rather than a UART with a migration path (D20). No shipping Espressif part has both a USB host
and an internal ethernet MAC, so the MAC goes and the W5500 comes in (D32).

| | Eth MAC | USB host | Available |
|---|---|---|---|
| ESP32 classic | internal | none | yes |
| **ESP32-S3** | no | OTG FS, 8 channels | **yes** |
| ESP32-P4 | internal | OTG HS, 16 ch | no, zero stock, bare QFN80 only |
| ESP32-S31 | internal, 1 Gbps | OTG HS | listed, **zero stock** |

**What remains open is procurement, not design.** The ESP32-S31 solves everything — internal gigabit MAC,
high-speed USB host, and the radio back — and is in mass production with modules listed at DigiKey, but at
zero stock. Get real lead times from Mouser, Farnell, LCSC or Espressif before committing a generation 2
mainboard. See D33.

**The cost of the interim answer is recorded in D32 and is real:** Art-Net broadcast in a large rig exceeds
what SPI ethernet can absorb, so sACN becomes a requirement.

### Q4 — Regulator selection for the high-power variants

`PWR-AF` inherits the AP63203 and AP63205, both 2 A parts, and they are fine there. `PWR-AT` and
`PWR-BT` need different parts: higher current, and a 24 V input rather than 12 V.

The problem is that the right current per rail follows from the IO board catalogue, which is mostly still
at concept level. **Decide the worst-case per-rail draw the range intends to support before selecting**,
rather than sizing for 71 W on every rail, which would be expensive and thermally silly.

Apply the same frequency-spread-spectrum criterion that drove D12. The emissions problem is larger here,
not smaller.

### Q5 — J1 connector — RESOLVED

**Würth 613004243121**, 2×2 right-angle socket. J2 and J3 use the 2×10 **613020243121** from the same
family. A 2×2 cannot enter a 2×10, so cross-mating is impossible by geometry and needs no extra keying.

The earlier concern about 2.54 mm being too tight for 1500 V was wrong. J1's pins are all cable-side
conductors within ±57 V of each other, and in a hipot test they are tied together and driven as one node
against Domain 2. J1 needs 57 V pin-to-pin and 1500 V to Domain 2, and only the second is a clearance
problem. The keep-out answers it.

### Q6 — 50 MHz oscillator — CLOSED

**No longer applicable.** There is no RMII interface, so there is no reference clock to supply or gate. The
W5500 runs from its own 25 MHz crystal. See D5 and D32.

### Q7 — RTL8201F footprint — CLOSED

**No longer applicable.** The W5500 integrates MAC and PHY, so there is no separate PHY. If the design ever
returns to an internal MAC, note that the availability picture has inverted since D4 was written: the
LAN8720AI shows 23,356 in stock at DigiKey in industrial grade while the RTL8201F is not listed.

### Q8 — Halo light ring — RESOLVED

**The connector contains no LEDs.** The ring is a light guide; two LEDs go on the mainboard, left and
right, per Detail X on Neutrik's drawing.

**They are addressable RGB, not single colour.** Two **`IN-PI15TAT5R5G5B`**, 1.5 × 1.5 mm and **0.65 mm
high** against Neutrik's 0.80 mm limit, cascaded on one data line from a single GPIO. Full colour on both
ring halves, which turns a bare "is it alive" indicator into a real status display.

**They need a level shifter.** Supply is 3.7 to 5.5 V, and a 3.3 V GPIO does not reliably clear the input
threshold. A **`74AHCT1G125W5-7`** sits between, TTL input thresholds at 5 V supply. See D32.

Two further facts from the connector datasheet, recorded in [03 — Mainboard](03-mainboard.md): it is rated
**−30 to +80 °C**, the lowest-rated part in the node, and it **does not intermate with Neutrik's CAT6 cable
connectors** `NE8MC6-MO` or `NKE6S*`.

---

## Blocking — decide before mechanical design

### Q9 — Thermal and chassis bonding for `PWR-BT`, together

These were two questions in the two-board design and they have become one, because the answers conflict.

**Thermal — much improved by D31, then made harder again by the enclosure.** A fully loaded `PWR-BT4` node
pulls 71.3 W through a sealed box and essentially all of it becomes heat. Efficiency decides where it
lands, not how much there is: at 93.8 % the AG59824-LPB keeps 4.4 W on the power board and the rest
spreads across the IO board's output devices.

**Coplanar boards changed the shape of this problem.** Stacked, the power board sat in the middle wrapped
by the other two. Edge to edge, all three lie flat and can couple straight into the enclosure floor, and
the mounting screws that take the connector strain are also the thermal path.

**But the enclosure is now 3D-printed, and that removes the floor as a heatsink.** PLA is around
0.13 W/mK against aluminium's ~200. The conducted path this section assumed does not exist in plastic, and
~8 W on a `PWR-BT4` board has to leave a closed box some other way.

Three consequences, none of them blocking but all of them real:

- **PLA is out.** Its glass transition is around 60 °C and the interior will plausibly reach that. PETG is
  marginal; ABS, ASA or PC are the realistic choices.
- **The module needs its own path out** — a heatsink with vents, or a metal insert in the floor. A metal
  insert brings back exactly the bonding conflict below, so decide the two together.
- **Derating bites.** The af module drops 12 W → 9 W between 70 and 85 °C at 12 V, and a hot enclosure
  spends the difference. The 24 V part holds 12 W flat, which is a thermal argument for 24 V even on the
  smallest variant.

**The good news is that 3D printing settles the bonding half.** A plastic enclosure has no conductive path
between boards, so `CHASSIS` cannot bridge to the power board through the mounting hardware and nylon
standoffs make the question disappear. What is left is genuinely thermal.

**Chassis bonding — now a population option rather than an open question.** PoE provides no protective
earth, so the chassis floats unless deliberately referenced. The mainboard carries `CHASSIS` as its own net
with the etherCON shield and the mounting holes on it, bonded to `GND` through a fitted 1 nF / 2 kV plus
1 MΩ, with a 0 Ω footprint for a hard bond. See D30. **What remains open is which of those to fit**, which
is an EMC test result rather than a design decision, and what an IO board does with its own cable shields.

**The conflict:** the thermal path from the power board to the enclosure is a conducted path, and the power
board is the board carrying the isolation barrier. A heatsink bonded to a metal enclosure is an electrical
connection whether you intended one or not, and it is on the wrong board to be casual about.

The etherCON helps. Its shield is isolated from panel ground and arrives on a PCB pin, and the `CHASSIS`
net keeps the mounting holes off logic ground, so two of the three paths are now deliberate. The third, an
IO board's own cable shields, is still open and belongs to whichever IO board introduces one.

**This has to be an explicit, documented scheme per enclosure variant.** It is the hardest remaining
mechanical question and it should be answered before any enclosure is chosen.

### Q10 — Board arrangement — RESOLVED, then re-answered

**Mainboard and power board side by side, IO board across the back of both.** See D35.

```
   ┌───────────────┬──────────────┐
   │   MAINBOARD   │ POWER BOARD  │
   │  ◀ etherCON   │   J1 · J2    │
   └───────┬───────┴──────┬───────┘
        J4 │           J3 │
   ┌───────┴──────────────┴───────┐
   │          IO BOARD            │
   └──────────────────────────────┘
```

**The earlier answer was a row: mainboard, power board, IO board.** Its reasoning stands and is why the
power board still sits between the other two electrically — a WS2812 board at 40 W and 5 V draws 8 A, and
2.54 mm contacts are rated 3 A, so the IO board's supply must not cross the mainboard. What the row could
not fix was the other direction: **the power board had to pass six signals through, USB among them**, and
D31 flagged that as needing rework for generation 2.

Putting the IO board across the back of both gives each board a direct connection to what it actually
exchanges. Both original goals survive, the passthrough disappears, and nothing has to be stacked.

**What it introduced:** a ground triangle instead of a chain, and a fixed spacing between J3 and J4 that
every IO board and every power board variant has to honour. Both are recorded in D35 and `06`.

**A stacked power board was considered and rejected** — thermally it discards D31's main argument, and
T1's 8.8 mm exceeds what a paired WR-PHD socket and header give (about 7.9 mm), so it would also have
needed an elevated socket as a special part.

### Q11 — XLR: 3-pin or 5-pin?

5-pin is the DMX512-A standard. 3-pin is what the haunt and small-venue market actually uses. Affects the
DMX IO board outline and the enclosure. Possibly both variants of the same PCB.

---

## Blocking — before the mains IO board exists

### Q12 — Functional switching or an isolation function?

Decides the component class entirely. Functional switching means a relay or contactor. Isolation for safety
means a load-break switch per IEC 60947-3, and relays are not suitable. See
[09 — Compliance & Safety](09-compliance-and-safety.md).

### Q13 — Is it safety-related?

If the disconnection is a safety function, EN ISO 13849 and EN 62061 apply, with Performance Level,
redundancy and mirror-contact feedback. **A network-commanded relay is categorically not a safety device.**
If the installation needs an emergency stop, it needs a hardwired one.

The RP2354's local failsafe does not change this and must not be allowed to look like it does.

### Q14 — De-energised state of the mains actuator

Should the load drop out when the node loses PoE? Safer, but "the lights went out because a switch
rebooted" is usually the worse outcome on site. A product decision, not a technical one. Bistable
contactors offer a third answer, and remove about 2 W of standing coil dissipation, at the cost of a harder
question about state after a power failure.

---

## Non-blocking, but decide early

### Q15 — Resident bootloader design for the RP2354

**Much less critical than it was.** With a USB link the mainboard can drive the RP2350's **ROM** bootloader
directly: assert `IO_BOOTSEL`, cycle `IO_nRESET`, and the chip enumerates as mass storage accepting a UF2.
A resident bootloader is now a convenience for routine updates rather than the only thing between a failed
update and an SWD probe.

Still worth designing carefully: what happens if power is lost mid-write, if an image fails verification,
if the link drops between blocks, if a new image boots but never re-identifies. Two application slots in
2 MB is comfortable; capacity was never the question.

Bringing the RP2354's USB out to a service connector as well gives a laptop a direct route with no
mainboard involvement at all.

### Q16 — Which EMC standard applies?

Lighting equipment (EN 55015 / EN 61547), professional entertainment (EN 55103-2, now largely EN 55035), or
industrial (EN IEC 61000-6-2 / -6-4)? Determines whether surge appears at the DMX signal port and therefore
whether the PPTC and series resistor footprints get populated. **Settle with the test lab, do not assume.**

Two emissions sources arrived with the three-board design and have not been assessed: switched-mode rails
travelling between three coplanar boards on 2.54 mm headers, and a 71 W PD module sat on a board with four
regulators and four eFuses, with the longest conductor in the product attached to it.

### Q17 — Licence and openness

Not decided. The repository is currently private. Options range from fully open hardware (CERN-OHL) with an
open firmware licence, through open documentation with closed hardware files, to entirely proprietary.

**D19 sharpened this.** A published third-party IO board interface is now a much safer thing to offer than
it was, because a serial link cannot brick the host the way an exposed GPIO could. The haunt and escape
room community has a strong maker culture, and an open interface specification would let third parties
build IO boards, which is exactly the ecosystem effect that makes the modular architecture pay off. That
does not require open-sourcing the mainboard.

### Q18 — Board type ID registries

Who allocates `board_type` values, for both power boards and IO boards? If third-party IO boards are ever
intended, a range must be reserved for them, and the descriptor formats in
[06 — Board Interfaces](06-board-interfaces.md) need a published specification, along with the link
protocol.

### Q19 — Production test strategy

Larger than it was, because there are now three boards and two firmware images.

At minimum: PoE negotiation and classification per power board variant, rail voltages and eFuse trip
points, metering calibration, EEPROM programming, link up, ESP32 flash read and write, SWD programming of
the RP2354's resident bootloader, and an isolation test on the DMX IO board.

**Design the test points in now.** The SWD programming step in particular is new and has to be reachable on
an assembled IO board.
