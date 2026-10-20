# 04 — Power Board

The power board takes four DC conductors from the mainboard's magnetics, negotiates with the PSE, provides
the isolation barrier, and hands back up to four protected and metered rails. It has no high-speed content
and can be a two-layer board.

It is the only board in the node that varies by more than population options, and it exists as a separate
board so that changing PoE class is a swap rather than a respin.

## Variants

Silvertel's SMT **`MTB`** and **`LPB`** families cover the whole range. Every variant is machine-placeable.
The 56 mm through-hole SIL module the two-board design was built around is gone, and so is the question of
whether to use it.

| Variant | 24 V module | 12 V module | Standard | PD budget | **Design for** | Size | Eff |
|---|---|---|---|---|---|---|---|
| `PWR-AF` | AG9924-MTB | AG9912-MTB | 802.3af | 12.95 W | **12 W @70 °C** | 21 × 14 × 13 | 87 % |
| `PWR-AT` | AG5724-LPB | AG5712-LPB | 802.3at | 25.5 W | **24 W** | 33 × 22 × 13 | 88.5 % |
| `PWR-BT3` | AG59624-LPB | AG59612-LPB | bt Type 3 | 51 W | **48 W** | 40 × 25 × 15.8 | 93 to 94 % |
| `PWR-BT4` | AG59824-LPB | AG59812-LPB | bt Type 4 | **71.3 W** | **67 W** | 50.6 × 30 × 14.7 | 94 to 95 % |

**Three numbers, and only the third is a design figure.** The PD budget is what the standard guarantees at
the PD. The module rating (12 / 30 / 60 / 100 W) is what the converter can peak at. **"Design for" is
Silvertel's own continuous recommendation**, and it is below both: "24W continuous, 30W peak" for the
Ag5700; "should be designed for a maximum continuous output power of 48W" for the Ag59600; 67 W for the
Ag59800 "where cabling and PSE cannot be specified", which in a haunt is always.

**90 to 100 W is not obtainable over PoE.** 90 W leaves the PSE, 71.3 W reaches the PD, 67 W is what
Silvertel stands behind. A node that needs more is a DC-input power board, not a PoE one.

**The af module derates with temperature, and only at 12 V.** Ag9912-MTB drops 12 W → **9 W** between 70
and 85 °C; Ag9924-MTB holds 12 W flat.

Stock at DigiKey, checked 2026-09-06: AG9912-MTB 2018, AG9924-MTB 2188, AG5712-LPB 1310, AG5724-LPB 959,
AG59612-LPB 288, **AG59624-LPB 8**, **AG59812-LPB 0**, AG59824-LPB 278. The bt parts are the thin ones and
the stock is oddly split: at Type 3 the 12 V part is available and the 24 V is not, and at Type 4 it is
the other way round. Check before committing a variant.

**The mainboard does not cap this table.** Both the etherCON and the Würth 7490220122 magnetics clear
802.3bt Type 4, so `PWR-BT4` is buildable without touching the board that cannot be swapped. That is the
whole point of D17 and D28.

### The module output voltage is a per-variant choice

Not a range-wide standard. Each power board picks whatever PD module suits it, does its own
down-conversion, and declares the resulting rails and their per-rail budgets in its descriptor. The
mainboard reads that, enforces the budget, and refuses to enable an IO board that asks for a rail this
board does not have.

**What is contractual is the descriptor, not the topology behind it.** A `PWR-BT` built on the AG59624-LPB
starts at 24 V and bucks down; one built on the AG59612-LPB starts at 12 V and has no 24 V rail at all.
Both are legitimate boards and the firmware handles both without knowing which is which, because it reads
`rails_present` and the four `limit_mA_*` fields rather than assuming a topology.

The AP63203 and AP63205 accept 3.8 to 32 V in, so they work directly from either 12 V or 24 V without
change.

### Outline: only the mating edge is fixed

**D31 relaxed this, D35 tightened it again by half.** With the boards coplanar rather than stacked, power
board variants do **not** have to share a full outline. What must match is the position of J1 and J2 on the
edge facing the mainboard, **J3 on the edge facing the IO board**, and the mounting holes that take the
mechanical load.

Because J3 has to line up with J4 on the mainboard, **the power board's width is fixed and only its depth
is free.** A `PWR-AF` may be shallow but not narrow.

So a `PWR-AF` carrying a 21 × 14 mm module can be a small board, and `PWR-BT4` with a 50.6 × 30 mm module
can be a large one. Under the earlier stacked design both had to be sized for the largest module, which
wasted area on the cheap variant for the sake of a common stack height.

The enclosure then has to accommodate the largest variant, but a smaller board simply leaves empty space
rather than forcing empty copper.

The corollary on the mainboard is unchanged: the etherCON and its magnetics are rated for the most
demanding variant even on a node fitted with the cheapest one.

## Input side

```
J1 ──▶ TCT  ┐                ┌── L1 L2 L3 ──┐
       RCT  ├──▶ BR1, BR2 ──▶┤              ├──▶ Silvertel PD module ──▶ isolated output
       SP45 │    (discrete)  └── L4 L5 L6 ──┘     · detection signature
       SP78 ┘                                     · fixed classification
                                                  · 1500 V isolation
```

Four conductors, each up to **960 mA** at Type 4. Two bridge rectifiers sort out polarity and mode, so J1
has no orientation requirement beyond getting the pairs to the right pins.

**Correction: the bridges are not inside the module.** An earlier version of this document said they were.
The Ag59800 block diagram and its Figure 15 show `BR1` and `BR2` as discrete parts ahead of `VIN+`/`VIN−`,
and the pin description says those pins "connect to the output of the POE input bridge rectifiers". One
bridge per pairset, each carrying the full 960 mA. The other families use identical wording and should be
checked individually rather than assumed.

**Silvertel's reference input stage** (Figure 15) also carries six ferrites, three in each leg —
`MPZ2012S102A` in 0805 — and **two 4.7 nF / 2000 V capacitors deliberately bridging the isolation barrier**
for EMC. That is the one sanctioned exception to "nothing crosses the barrier", and it has to appear in the
clearance drawing rather than being discovered at layout. Würth `885342211008` is the part and it is
already in Part-DB.

Everything on the input side of the module's transformer is **Domain 1** and must hold isolation-grade
clearance from every rail, the I2C, and the ground pour. Keep the barrier a straight line across the board.

Note that J1's own pin-to-pin spacing only sees 57 V. The 1500 V is between Domain 1 and Domain 2, which is
a board clearance question, not a connector one.

### Classification

This is where the variants earn their keep, and it matters more than it sounds.

**Every SMT module in the range is fixed-class. Classification programming is not available.** This was
assumed for a long time and it is wrong — see the correction in D13.

| Module | Class | Reserves |
|---|---|---|
| Ag9900-MTB | fixed **Class 0** | 15.4 W |
| Ag5700-LPB | fixed Type 2 **Class 4** | 30 W |
| Ag59600-LPB | fixed Type 3 **Class 6** | 60 W |
| Ag59800-LPB | fixed Type 4 **Class 8** | 90 W |

The consequence is the one the old text warned about: a `PWR-AF` node drawing 3 W still reserves 15.4 W, so
roughly 24 nodes on a 370 W switch instead of 90. It is worse at the top — `PWR-BT3` reserves 60 W, six
ports on that same switch. **The high variants are therefore what you fit because the load needs them,
never a default.** The programmable-class part is the through-hole Ag9700, which D29 retired.

**What the bt modules give instead is `TYP2-DET` and `TYP3-DET`.** These flag which PSE type actually
turned up, so a `PWR-BT3` on an at switch knows it has 25.5 W rather than 51 W and can say so. They pull low
from `VIN+` and therefore sit in Domain 1: they reach the mainboard through an optocoupler, which is
Silvertel's own arrangement and the sanctioned way across the barrier.

The declared class goes in the descriptor EEPROM and the detected type is read at boot, so the node can
report over MQTT both what it asked the switch to reserve and what it actually got. Compare that against
the metered draw and PoE budget exhaustion stops being a mystery.

## Output side

```
PD module
   │
   ├── 24 V ──▶ eFuse ──▶ INA226  0x44 ──┐
   ├── 12 V ──▶ eFuse ──┐                │
   │                    ├── INA3221 0x40 ├──▶ J2
   └──▶ buck ── 5 V ──┬▶ eFuse ───────────┤   to J3, behind PWR_EN
                      └▶ eFuse ───────────┘   to J2, unconditional
                                          + 24AA02UID 0x50
```

Only 5 V is guaranteed across all variants, and there is no 3.3 V rail: every board regulates its own from
5 V (D36). The descriptor says what else exists and at what limit, and
firmware refuses to enable an IO board that asks for a rail the fitted power board does not have.

### Regulators

Independent bucks from the module's output, not cascaded. Cascading multiplies efficiencies and couples
the rails.

For `PWR-AF` one buck: **AP63205WU-7** for 5 V, 3.8 to 32 V in, 2 A, TSOT-26, with frequency spread
spectrum of about ±6 % jitter, which is why it was chosen over a cheaper MP2315 or TPS562201. Its sibling
**AP63203WU-7** made 3.3 V here until D36 moved that job to the mainboard, where an AMS1117 LDO does it. The board already carries
the PD module's switcher and, on some IO boards, an isolated DC/DC. Two more fixed-tone 1.1 MHz emitters
would be an avoidable EMC problem.

**The `PWR-BT3` and `PWR-BT4` regulators are not yet selected.** A 2 A part is not enough when 67 W is
available, and on a 24 V-input build the step-down ratio is larger.

**`PWR-BT4` is a 24 V board**, because the 12 V AG59812-LPB is unavailable and because 24 V halves the
secondary current — 4.2 A instead of 8.4 A at the module, and 2.8 A instead of 5.6 A on J3 at full load.
That means two bucks (24→12, 24→5) rather than one, and it makes the 24 V eFuse mandatory rather than
optional. Current per rail, and therefore the regulator selection,
follows from the IO board catalog rather than from the PD module. See Q4 in
[11 — Open Questions](11-open-questions.md).

### Per-rail eFuse

Every rail leaves through a current-limiting load switch with a hard trip that does not depend on firmware.

At 12.95 W a shorted rail is a brownout. At 71 W it is a fire, and the Silvertel's own protection sits at
the 48 V input where it will happily deliver 71 W into a shorted 5 V rail without noticing anything is
wrong. The trip has to be local to the rail and it must not need the ESP32 to be alive, because the ESP32
is powered from one of the rails being protected.

Three things fall out of the eFuse for free, and they are worth having in their own right:

- **Defined inrush.** An IO board with bulk capacitance no longer collapses the rail for everything else
  when it is enabled.
- **Power cycling a locked-up IO board** without power cycling the node.
- **Sequenced startup.** Rails come up in a defined order rather than racing.

**TPS259803ONRGER is already in Part-DB and is the right part for 12 V and both 5 V branches.** 2.7 to 24 V, 8 A,
with programmable current limit, circuit breaker, overvoltage protection, slew rate and UVLO. $2.64,
7,916 in stock, 24-VQFN.

| Rail | Part | Note |
|---|---|---|
| 24 V | **not selected**, TPS1663 class at 60 V | The TPS259803's 24 V ceiling leaves no margin here |
| 12 V | TPS259803ONRGER | 2× margin |
| 5 V to J3 | TPS259803ONRGER | Same part, different sense resistor. Behind `PWR_EN` |
| 5 V to J2 | TPS259803ONRGER, enable tied high | The unconditional mainboard feed. Keeps current limit and inrush without firmware |

Using one eFuse part across three rails is worth more than optimizing each one. Same footprint, same
driver code, one line in the BOM.

Pick the trip thresholds from the declared per-rail limits in the descriptor, not from what the regulator
can deliver. The eFuse protects the IO board's wiring and the enclosure, not the regulator.

### Metering

Voltage, current and power per rail, read by the mainboard over the I2C on J2.

**INA3221** covers 12 V, the IO board's 5 V and the mainboard's 5 V feed. Three channels, one package, one
address, which is exactly the shape of the problem. The third channel metered 3.3 V until D36; metering the
mainboard feed separately means the node's own consumption is visible apart from the IO board's.

**It cannot be used on the 24 V rail.** Its common-mode range tops out at 26 V. A 24 V rail with switching
ripple and load transients on it has no usable margin against that, and the failure is a measurement that
quietly goes wrong rather than a part that dies. The 24 V rail gets an **INA226** at 36 V instead.

The raw 48 V PoE input is deliberately not metered. It is in Domain 1, so its I2C would have to cross the
isolation barrier, and nothing crosses the barrier. Input power is inferred from the output rails and the
module's rated efficiency, which is close enough for a health topic and a budget check.

Metering is not only diagnostics. Firmware compares an IO board's declared worst-case draw against what the
fitted power board actually offers, refuses to release the IO board from reset if it does not fit, and then
watches the real figure at runtime. See [08 — Firmware & Network](08-firmware-and-network.md).

## Identification

**24AA02UID at I2C address 0x50.** 2 kbit EEPROM with a factory-programmed globally unique serial in the
upper page, so every power board has a traceable identity with no programming step in production.

The metering already puts I2C on J2, so the EEPROM costs about $0.15 and one more address on a bus that
exists. It reuses the descriptor format drafted for the two-board design, and it carries the things the
mainboard cannot work out for itself: which rails exist, what each one is limited to, the shunt calibration
constants the metering needs, and the declared PoE class.

Descriptor layout and field definitions are in [06 — Board Interfaces](06-board-interfaces.md).

Strap pins were considered and rejected. They read before I2C is up, which sounds useful, but they carry no
serial and no calibration data, and the mainboard has nothing useful to do in the window before I2C
initializes anyway. It cannot enable an IO board until it knows the budget, and it does not know the budget
until it has read the descriptor.

## Thermal

**This is the section that separates the variants**, and on `PWR-BT` it is the hardest problem on the
board.

| | `PWR-AF` | `PWR-BT4` |
|---|---|---|
| PD module dissipation | ~1.5 W | ~4.4 W at 93.8 % and full load |
| Regulators and eFuses | ~0.4 W | up to ~4 W |
| Total on this board | ~2 W | **~8 W** |

**Efficiency decides where the heat lands, not how much there is.** A fully loaded `PWR-BT4` node pulls
71.3 W through a sealed box and all of it becomes heat somewhere. What 93.8 % buys is that only 4.4 W of it
lands here, on a surface-mount part lying flat, rather than the 10.5 W an earlier draft put into a DIL
Ag5800 standing proud of the board. The rest is spread across the IO board's output devices, which is a
much easier shape to cool than one hot module.

Eight watts on this board is still real, and a fully loaded node wants a conducted path from the module to
the enclosure wall. That collides with the chassis bonding question, because a
thermal path is usually also an electrical one, and this board carries the isolation barrier. Both are open,
but they are a design task now rather than a threat to the concept. See Q9 in
[11 — Open Questions](11-open-questions.md).

Silvertel specify a component keep-out around their modules. Pull it from the datasheet for the specific
part rather than assuming the af figure applies to the bt one.

The eFuses also want thought here. A part in current limit dissipates the full rail voltage across itself
at the limit current, which on 24 V at 1 A is 24 W in a small package. That is a fault condition rather than
a steady state, but the part has to survive it long enough to trip.
