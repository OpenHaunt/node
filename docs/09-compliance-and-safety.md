# 07 — Compliance & Safety

Nothing here is legal advice. It is a record of what was established during design so the questions do not
have to be rediscovered, and so the expensive ones get asked early.

## The architectural decision that governs everything

**The mainboard and the power board's output side are SELV-only and must stay that way.** Every IO board
that introduces a hazardous voltage carries its own barrier, on that board.

This is not tidiness. It means:

- The mainboard plus power board gets **one** EMC assessment and **one** product file
- The radio approval is inherited from the Espressif module
- A mains-switching IO board does not drag the entire product line into the Low Voltage Directive
- IO boards can be developed, changed and re-certified independently

If mains ever appears on the mainboard, this collapses and every node in the range becomes a mains product.

### What the three-board split changed

One thing got harder and one got easier.

**Harder: the cable-side domain now spans two boards.** In the two-board design, Domain 1 began and ended
on one PCB. Now un-isolated PoE at 37 to 57 V leaves the mainboard on J1, crosses a connector, and runs
across the power board to the PD module's input. The 1500 V barrier is two barriers on two different
boards, with a connector in between. J1 is a separate 4-pin connector with a keep-out for exactly this
reason, and the assessment has to treat the mainboard and power board as one assembly.

**Easier: the etherCON shield is isolated from panel ground.** The NE8FAH-LR-DAE brings the cable shield
to a PCB pin rather than bonding it to the panel, so where the shield goes is decided on the schematic
instead of by how the connector was mounted.

The mainboard uses that: `CHASSIS` is a separate net carrying the shield and the mounting holes, bonded to
`GND` only through a fitted 1 nF / 2 kV and 1 MΩ, with a 0 Ω footprint for a hard bond if testing prefers
one. The capacitor gives ESD and HF a path, the resistor prevents static build-up, and neither passes DC
circulating current. **This does not answer the bonding question, it makes the answer a population option**
that the test lab can settle without a respin. See D30.

## Radio approval

**The design uses the `ESP32-S3-WROOM-1`, a PCB-antenna module (D32).** That removes this entire section
from the critical path: an integrated PCB antenna carries its modular approval with no antenna argument at
all. No U.FL, no pigtail, no bulkhead, no gain figure to defend, no enclosure penetration.

**The radio is present but unused** (Q2). It still has to appear in the technical file, and the module's own
approval covers it whether or not the firmware enables it.

The rest of this section is kept because it is the reason the decision went that way, and because it comes
straight back if anyone proposes an external antenna for a metal enclosure.

The ESP32-WROOM-32UE holds Single Modular Approval, **FCC ID 2AC7Z-ESPWROOM32UE**, under FCC Part 15C
§15.247, with equivalent CE/RED and other regional approvals.

Inheriting it depends on the antenna:

> 2.4 GHz · 50 Ω · **monopole type** · **peak gain ≤ 2.33 dBi**

2.33 dBi is the gain of the antenna Espressif certified with. Exceed it, or change antenna type, and the
modular approval no longer covers the product — you are into your own radiated emissions testing.

### The practical trap

Almost every antenna sold as a "Wi-Fi antenna" advertises 3, 5 or 8 dBi. All of those are out of bounds.
Finding a **≤2.33 dBi** antenna is paradoxically harder than finding a high-gain one.

A concrete example of how easy this is to get wrong: the Linx **ANT-2.4-CW-HWR** is the obvious mechanical
choice — articulated base, tilts 90°, rotates 360° — and its datasheet specifies **3.2 dBi maximum gain**. It
cannot be used.

**If an external antenna is ever needed:** Taoglas **GW.26.0151**, RP-SMA(M), monopole, **1.8 dBi**, IP65,
30.3 mm. Both conditions satisfied, with a real datasheet stating the gain. It is not in the current BOM.

### Buy the antenna from a distributor with a datasheet

The compliance argument rests on a documented gain figure that goes into the technical file. A datasheet from
Taoglas or TE/Linx with specified gain, VSWR and radiation pattern is evidence. A one-page sheet from an
unbranded vendor stating "2 dBi" with no measurement conditions is not.

The antenna is not an SMT part — it never enters the assembly flow — so there is no benefit in sourcing it
from the same distributor as the PCB components, and a real cost to doing so. Cheap unbranded antennas are
fine for prototypes and bring-up; they should not be in the production BOM.

## EMC

### Which standard applies

This has to be settled with the test lab, not assumed. Candidates depending on how the product is classified:

- **Lighting equipment** — EN 55015 (emissions) + EN 61547 (immunity)
- **Professional entertainment equipment** — historically EN 55103-2, now largely superseded by EN 55035
- **Industrial equipment** — EN IEC 61000-6-2 (immunity) + EN IEC 61000-6-4 (emissions)

The standards landscape here has shifted repeatedly in recent years. **Do not rely on a design-time
assumption; get it confirmed.**

### Surge at the DMX port — probably not required

Surge (IEC 61000-4-5) is applied primarily at the mains input, not at signal and control ports. At the DMX
port the normal tests are:

- **ESD (61000-4-2)** — ±4 kV contact / ±8 kV air. Always applicable. The SM712's ±30 kV is a wide margin.
- **Burst / EFT (61000-4-4)** — ±0.5–1 kV capacitively coupled. Also absorbed by the TVS.
- **Conducted RF (61000-4-6)** — unrelated to the protection network.

**The exception that catches DMX:** surge is applied to signal ports when the connected line exceeds 30 m or
leaves the building. A DMX segment is specified to ~300 m, so if the product is assessed under
**EN IEC 61000-6-2**, the 30 m rule applies and 1 kV line-to-ground surge appears at the signal port.

This is why the PPTC and 10 Ω series resistors are **footprints with 0 Ω fitted** rather than either omitted
or populated. The decision can be deferred to whatever the lab says, or to a specific installation, without a
board revision.

### The thing that matters more than the certificate

In permanent installations — building-to-building cabling, exterior architectural lighting — lightning-induced
transients are what actually destroys equipment. That protection gets fitted because otherwise devices die
after the first summer thunderstorm, not because a test report demands it. A node sitting in a rack in the
same room as the console has no such exposure.

### EMC-driven component choices

Three parts were chosen partly or wholly for emissions reasons:

- **AP63203 / AP63205** bucks with frequency spread spectrum, ±6 % jitter. The stack already carries the PD
  module's switcher, and a DMX IO board adds an isolated DC/DC. Two more fixed-tone 1.1 MHz switchers would
  have been an avoidable problem. **Apply the same criterion when selecting the high-power variants'
  regulators**, where the currents and therefore the emissions are much larger.
- **CA-IS2092VW** with factory slew-rate limiting — 250 kBaud does not need fast edges, and unnecessarily fast
  edges produce reflections and radiation.
- The isolated DC/DC inside the DMX transceiver switches in the high-MHz range. Take the manufacturer's
  keep-out and ferrite guidance seriously. ADI's **AN-1349** covers the same class of problem for the
  ADM2587E and is worth reading even though that part was not selected.

Two new emissions sources arrived with the three-board design and neither has been assessed:

- **The board-to-board rails.** J2 and J3 carry switched-mode rails on 2.54 mm headers between three
  coplanar PCBs. Those are unshielded conductors of a few centimetres carrying converter ripple, which is a
  more effective antenna than any trace on a board. The coplanar arrangement makes them shorter than a
  stack would have, but also puts them near the enclosure floor rather than buried between boards. Bulk and high-frequency decoupling at both ends of both
  connectors, and the ground pins on J3 flanking the link pair are there for return path as much as for
  the future USB pair.
- **A 71 W PD module.** The AG598xx-LPB is a considerably larger switcher than anything in the af design,
  sat on a board that also carries four regulators and four eFuses. Conducted emissions back onto the
  ethernet cable are the thing to watch, and the cable is the longest conductor attached to the product.

## SELV and the mains module

### What changes when mains enters the picture

Every barrier in the base design is **SELV to SELV**: PoE to logic, logic to DMX. Introducing mains means
**reinforced insulation to SELV**, because ethernet and DMX are touchable, operator-accessible connections.

- Creepage and clearance per IEC 60664-1. For 230 V, basic insulation is roughly 3 mm; reinforced more like
  6–8 mm. For **400 V between line conductors** the figures are larger again. The exact values depend on
  overvoltage category, pollution degree and material group — **take them from the tables, do not estimate**.
- The product falls under a different regime: EN 61010 or EN 60947 rather than a pure EMC assessment, plus the
  Low Voltage Directive, plus protective earthing requirements.

On a compact board sharing an enclosure with two others, 6 to 8 mm of creepage plus a four-pole contactor
is not a realistic outline. **This is why mains IO boards are remote, and in practice why they get their own node.**

### The relay is the barrier

The clean construction: a proper power relay or contactor specifies **reinforced insulation between coil and
contacts**, typically 5 kV withstand and at least 8 mm creepage. The control side, including the coil drive,
the RP2354, J3 and the entire mainboard, stays SELV. Mains exists only on the contact side.

The board layout must then preserve that separation. The slot under and around the relay is not optional.

### Neutral switching

If neutral is switched, **the neutral pole must not open before the line conductors and must close no later
than them.** Otherwise, with unbalanced load, the star point shifts and single-phase equipment can briefly see
up to 400 V.

Four independent relays cannot guarantee this — operate and release times vary between samples, with
temperature and with coil voltage. **Use one four-pole device with a single coil**, where the pole sequencing
is defined and tested by the manufacturer.

### Isolation is not switching

These are different functions and the distinction decides the component class:

- **Functional switching** — turning a load on and off. A relay or contactor is appropriate.
- **Isolation (disconnection for safety)** — making equipment safe to work on. Relays are **not** suitable:
  they lack the defined contact gap, the reliable position indication and the lock-off capability required of
  isolating devices (IEC 60947-3).

If anyone could ever read the device state as "the installation is dead", the product needs a load-break
switch, not an actuator.

### Safety functions

If the disconnection is safety-related — emergency stop, making a rig safe for people — it falls under
EN ISO 13849 / EN 62061, requiring a defined Performance Level, redundant contacts and a feedback loop through
mirror contacts.

**A relay commanded over Art-Net or MQTT is categorically not a safety device**, regardless of how carefully
the firmware is written. Network commands are not fail-safe, and the node is not a safety controller. If the
installation needs an emergency stop, it needs a hardwired emergency stop.

The RP2354 does not change this. A local failsafe on the IO board makes the product better behaved when the
link drops, and it is still not a safety function. Do not let "the IO board holds a defined state" turn
into a safety argument in anybody's head.

This is recorded as a hard product boundary in [01 — Product Concept](01-product-concept.md).

## Temperature grading

The thermal analysis in [02](02-system-architecture.md) no longer describes one design. An af node
dissipates 3 to 11 W and a fully loaded `PWR-BT4` node dissipates up to 71 W, which are different products
thermally.

For the af case the old figures stand: a plausible 65 to 75 °C internal in a sealed metal enclosure in a
warm venue. Check every part against that, not against a comfortable assumption.

| Part | Rating | Verdict |
|---|---|---|
| W5500 | −40…+85 °C | Adequate |
| CA-IS2092VW | −40…+125 °C | Ample |
| ESP32-S3-WROOM-1 | −40…+85 °C | Adequate |
| RP2354A / RP2354B | −40…+85 °C | Adequate. The stacked flash die is the part to check, not the MCU |
| AP63203 / AP63205 | −40…+85 °C, check | Verify against junction temperature, not ambient |
| AG59824-LPB | −40 to +85 °C | **Derating against ambient is the design problem, not the rating** |
| Würth 7490220122 | −40 to +105 °C | Ample |
| **Neutrik NE8FAH-LR-DAE** | **−30 to +80 °C** | **The lowest-rated part in the node.** See below |
| Würth 150060xS75000 LEDs | −40 to +85 °C | Adequate |

**For `PWR-BT` this table is not the analysis, it is the easy part.** Silvertel derate their modules
against ambient, so the number that matters is the temperature inside the enclosure next to the module,
not the venue temperature. At 93.8 % the AG59824-LPB contributes 4.4 W, which is tractable; the IO board
is now the larger heat source in a loaded node. See Q9 in [11 — Open Questions](11-open-questions.md).

Continuously energised relay coils add about 2 W and are a real input to this analysis, not a rounding
error.

## PoE and the installation

Two things that are not the product's compliance problem but will become its support problem.

**802.3bt Type 4 needs a Type 4 PSE.** A 90 W port is not a common thing on a cheap switch, and a bt node on an
at switch will either negotiate down to a lower class or fail to power up. The node reports its declared
class and its measured draw over MQTT precisely so this is visible rather than mysterious.

**Switch budget is finite and shared.** One `PWR-BT4` node can consume the budget of six af nodes.
This is worth saying out loud to installers, because PoE budget exhaustion usually presents as a port that
silently refuses to power up, which looks exactly like a dead node.
