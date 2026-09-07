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

| Variant | 24 V module | 12 V module | Standard | PD budget | Module out | Size | Eff |
|---|---|---|---|---|---|---|---|
| `PWR-AF` | AG9924-MTB | AG9912-MTB | 802.3af | 12.95 W | 12 W | 21 × 14 × 13 | 87 % |
| `PWR-AT` | AG5724-LPB | AG5712-LPB | 802.3at | 25.5 W | 30 W | 33 × 22 × 13 | 88.5 % |
| `PWR-BT3` | AG59624-LPB | AG59612-LPB | bt Type 3 | 51 W | 51 W | 40 × 25 × 15.8 | 93 to 94 % |
| `PWR-BT4` | AG59824-LPB | AG59812-LPB | bt Type 4 | **71.3 W** | 100 W | 50.6 × 30 × 14.7 | 94 to 95 % |

**PD budget is the standard's guaranteed power at the PD. Module out is the converter's own rating.** They
are not the same and the gap is widest at the top: the AG59824-LPB is a 100 W converter, but a compliant
Class 8 link guarantees 71.3 W. **Budget on the standard.** The extra converter capability is headroom for
a generous PSE, not power you may design around.

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

**D31 relaxed this.** With the boards coplanar rather than stacked, power board variants do **not** have to
share an outline. What must match is the position of J1 and J2 on the mating edge, and the mounting holes
that take the mechanical load. Everything beyond that edge is free.

So a `PWR-AF` carrying a 21 × 14 mm module can be a small board, and `PWR-BT4` with a 50.6 × 30 mm module
can be a large one. Under the earlier stacked design both had to be sized for the largest module, which
wasted area on the cheap variant for the sake of a common stack height.

The enclosure then has to accommodate the largest variant, but a smaller board simply leaves empty space
rather than forcing empty copper.

The corollary on the mainboard is unchanged: the etherCON and its magnetics are rated for the most
demanding variant even on a node fitted with the cheapest one.

## Input side

```
J1 ──▶ TCT  ┐
       RCT  ├──▶ Silvertel PD module ──▶ isolated output
       SP45 │     · detection signature
       SP78 ┘     · classification
                  · integrated bridge rectifiers
                  · 1500 V isolation
```

Four conductors, each up to **960 mA** at Type 4. The bridge rectifiers inside the module sort out polarity
and mode, so J1 has no orientation requirement beyond getting the pairs to the right pins.

Everything on the input side of the module's transformer is **Domain 1** and must hold isolation-grade
clearance from every rail, the I2C, and the ground pour. Keep the barrier a straight line across the board.

Note that J1's own pin-to-pin spacing only sees 57 V. The 1500 V is between Domain 1 and Domain 2, which is
a board clearance question, not a connector one.

### Classification

This is where the variants earn their keep, and it matters more than it sounds.

The Ag9700 family presents a **Class 0 signature by default**, which makes the switch reserve **15.4 W per
port** for a device drawing 1.3 to 4 W. On a 370 W switch that is roughly 24 nodes instead of 90. For a
product whose scaling story is "hang another one next to it", that is the difference between a design that
scales and one that does not.

Use the classification-programmable variant on every power board and set the class from the real budget.
A `PWR-AF` feeding a reed switch board should be declaring Class 1, not Class 0.

The declared class also goes in the descriptor EEPROM, so the node can report over MQTT what it asked the
switch to reserve. Compare that against the metered draw and PoE budget exhaustion stops being a mystery.

## Output side

```
PD module
   │
   ├── 24 V ──▶ eFuse ──▶ INA226  0x44 ──┐
   ├── 12 V ──▶ eFuse ──┐                │
   │                    ├── INA3221 0x40 ├──▶ J2
   ├──▶ buck ── 5 V ──▶ eFuse ───────────┤
   │                                     │
   └──▶ buck ── 3V3 ──▶ eFuse ───────────┘
                                          + 24AA02UID 0x50
```

Only 3.3 V is guaranteed across all variants. The descriptor says what else exists and at what limit, and
firmware refuses to enable an IO board that asks for a rail the fitted power board does not have.

### Regulators

Two independent bucks from the module's output, not cascaded. Cascading multiplies efficiencies and couples
the rails.

For `PWR-AF` the parts from the two-board design carry over unchanged: **AP63205WU-7** for 5 V and
**AP63203WU-7** for 3.3 V, both 3.8 to 32 V in, 2 A, TSOT-26. Both use frequency spread spectrum with about
±6 % jitter, which is why they were chosen over a cheaper MP2315 or TPS562201. The board already carries
the PD module's switcher and, on some IO boards, an isolated DC/DC. Two more fixed-tone 1.1 MHz emitters
would be an avoidable EMC problem.

**The `PWR-BT3` and `PWR-BT4` regulators are not yet selected.** A 2 A part is not enough when 71 W is
available, and on a
24 V-input build the step-down ratio is larger. Current per rail, and therefore the regulator selection,
follows from the IO board catalogue rather than from the PD module. See Q4 in
[11 — Open Questions](11-open-questions.md).

### Per-rail eFuse

Every rail leaves through a current-limiting load switch with a hard trip that does not depend on firmware.

At 12.95 W a shorted rail is a brownout. At 71 W it is a fire, and the Silvertel's own protection sits at
the 48 V input where it will happily deliver 71 W into a shorted 3.3 V rail without noticing anything is
wrong. The trip has to be local to the rail and it must not need the ESP32 to be alive, because the ESP32
is powered from one of the rails being protected.

Three things fall out of the eFuse for free, and they are worth having in their own right:

- **Defined inrush.** An IO board with bulk capacitance no longer collapses the rail for everything else
  when it is enabled.
- **Power cycling a locked-up IO board** without power cycling the node.
- **Sequenced startup.** Rails come up in a defined order rather than racing.

**TPS259803ONRGER is already in Part-DB and is the right part for 12 V, 5 V and 3.3 V.** 2.7 to 24 V, 8 A,
with programmable current limit, circuit breaker, overvoltage protection, slew rate and UVLO. $2.64,
7,916 in stock, 24-VQFN.

| Rail | Part | Note |
|---|---|---|
| 24 V | **not selected**, TPS1663 class at 60 V | The TPS259803's 24 V ceiling leaves no margin here |
| 12 V | TPS259803ONRGER | 2× margin |
| 5 V, 3.3 V | TPS259803ONRGER | Same part, different sense resistor |

Using one eFuse part across three rails is worth more than optimising each one. Same footprint, same
driver code, one line in the BOM.

Pick the trip thresholds from the declared per-rail limits in the descriptor, not from what the regulator
can deliver. The eFuse protects the IO board's wiring and the enclosure, not the regulator.

### Metering

Voltage, current and power per rail, read by the mainboard over the I2C on J2.

**INA3221** covers 12 V, 5 V and 3.3 V. Three channels, one package, one address, which is exactly the shape
of the problem.

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
initialises anyway. It cannot enable an IO board until it knows the budget, and it does not know the budget
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
