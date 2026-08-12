# 07 — Compliance & Safety

Nothing here is legal advice. It is a record of what was established during design so the questions do not
have to be rediscovered, and so the expensive ones get asked early.

## The architectural decision that governs everything

**The base board is a SELV-only device and must stay that way.** Every module that introduces a hazardous
voltage carries its own barrier, on the module.

This is not tidiness. It means:

- The base board gets **one** EMC assessment and **one** product file
- The radio approval is inherited from the Espressif module
- A mains-switching module does not drag the entire product line into the Low Voltage Directive
- Modules can be developed, changed and re-certified independently

If mains ever appears on the base board, this collapses and every node in the range becomes a mains product.

## Radio approval

The ESP32-WROOM-32UE holds Single Modular Approval — **FCC ID 2AC7Z-ESPWROOM32UE**, FCC Part 15C §15.247,
with equivalent CE/RED and other regional approvals.

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

**Selected:** Taoglas **GW.26.0151** — RP-SMA(M), monopole, **1.8 dBi**, IP65, 30.3 mm. Both conditions
satisfied, with a real datasheet stating the gain.

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

- **AP63203 / AP63205** bucks with Frequency Spread Spectrum (±6 % jitter). The board already carries the PoE
  module's switcher, and DMX modules add an isolated DC/DC. Two more fixed-tone 1.1 MHz switchers would have
  been an avoidable problem.
- **CA-IS2092VW** with factory slew-rate limiting — 250 kBaud does not need fast edges, and unnecessarily fast
  edges produce reflections and radiation.
- The isolated DC/DC inside the DMX transceiver switches in the high-MHz range. Take the manufacturer's
  keep-out and ferrite guidance seriously; ADI's **AN-1349** covers the same class of problem for the
  ADM2587E and is worth reading even though that part was not selected.

## SELV and the mains module

### What changes when mains enters the picture

Every barrier in the base design is **SELV to SELV** — PoE to logic, logic to DMX. Introducing mains means
**reinforced insulation to SELV**, because Ethernet and DMX are touchable, operator-accessible connections.

- Creepage and clearance per IEC 60664-1. For 230 V, basic insulation is roughly 3 mm; reinforced more like
  6–8 mm. For **400 V between line conductors** the figures are larger again. The exact values depend on
  overvoltage category, pollution degree and material group — **take them from the tables, do not estimate**.
- The product falls under a different regime: EN 61010 or EN 60947 rather than a pure EMC assessment, plus the
  Low Voltage Directive, plus protective earthing requirements.

On a compact board carrying a PoE module, RJ45, a module connector and an isolation slot, 6–8 mm of creepage
plus a four-pole contactor is not a realistic outline. **This is why mains modules are remote, not stacked.**

### The relay is the barrier

The clean construction: a proper power relay or contactor specifies **reinforced insulation between coil and
contacts** — typically 5 kV withstand and ≥8 mm creepage. The control side, including the coil drive, the
module connector and the entire base board, stays SELV. Mains exists only on the contact side.

The module layout must then preserve that separation — the slot under and around the relay is not optional.

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

This is recorded as a hard product boundary in [01 — Product Concept](01-product-concept.md).

## Temperature grading

The thermal analysis in [02](02-system-architecture.md) puts internal temperatures at a plausible 65–75 °C in
a sealed metal enclosure in a warm venue. Check every part against that, not against a comfortable assumption:

| Part | Rating | Verdict |
|---|---|---|
| RTL8201FI-VC-CG | −40…+85 °C | Chosen **specifically** for this — the non-`I` variant is commercial grade |
| CA-IS2092VW | −40…+125 °C | Ample |
| ESP32-WROOM-32UE | −40…+85 °C | Adequate; `-H4`/`-H8` variants reach 105 °C if needed |
| W25Q128JVSIQ | −40…+85 °C | Adequate |
| AP63203 / AP63205 | −40…+85 °C (check) | Verify against the junction temperature, not ambient |

Continuously energised relay coils add ~2 W and are a real input to this analysis, not a rounding error.
