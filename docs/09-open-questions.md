# 09 — Open Questions

Everything that must be resolved before a schematic is worth drawing, roughly in the order it blocks progress.

---

## Blocking — decide before schematic

### Q1 — PoE module: SIL or SMT?

The **Ag9700 is a SIL module, 56 × 18 × 14 mm.** Fifty-six millimetres of length effectively defines the board
outline and works against the compact form factor the product wants.

Silvertel's SMT families (**Ag9900-MTB**, **Ag99xx-LPB**) are around **21.7 × 14.4 × 8.2 mm**, auto-placeable
and available in tape and reel — roughly a third of the footprint area and machine-assemblable rather than a
hand-fitted through-hole part.

**Action:** pull the individual datasheets for the SMT families and check the 12 V variant's output power
against the budget. The selector guide's per-voltage power figures could not be read reliably from the
combined chart, so this needs the actual datasheet. **Do not commit to the Ag9712-2BR until this is checked.**

### Q2 — Is Wi-Fi needed at all?

The device is PoE-powered and therefore always has an Ethernet cable attached. Configuration happens through
the web UI; discovery through mDNS. That is how Art-Net nodes conventionally work.

**Dropping Wi-Fi removes:** the U.FL antenna, the pigtail, the bulkhead connector, the enclosure penetration,
the strain relief, the antenna gain compliance argument, and the sourcing risk of the UE module variant
(~930 in stock versus ~13,000 for the PCB-antenna version). It also saves roughly $7 per unit.

**Keeping it buys:** an emergency access path when the network or IP configuration is broken. In a maze
ceiling, that is not nothing.

This should be a deliberate decision, not something that happens by default.

### Q3 — Module connection mechanics

Board-to-board headers, pogo pins, or card edge? See [04 — Module Interface](04-module-interface.md) for the
trade-offs. Current preference is **stacking headers with M3 standoffs** for the first prototype, since it
removes a category of risk while the electrical interface is unproven, and moving to pogo pins later is a base
board revision rather than an ecosystem change.

**The humidity and fog-fluid environment of a haunt is a genuine argument against pogo pins** that would not
apply in most other products.

### Q4 — Maximum module power, and therefore 802.3af versus 802.3at

802.3af gives 12.95 W at the powered device; the base board leaves ~10.4 W for modules. A long WS2812 run or a
large contactor blows through that.

Moving to 802.3at afterwards is a base board respin. **This has to be decided now**, based on the most
demanding module the range is intended to support.

### Q5 — RJ45 PoE magjack selection

Not yet chosen. Requirements: centre taps brought out, DC bias tolerance (typically 350 mA per pair), 1500 V
isolation, integrated LEDs. A standard cheap magjack such as an HR911105A will saturate and is not suitable.
Bob Smith termination on the unused pairs must be DC-blocked.

### Q6 — 50 MHz oscillator selection

Needs an enable pin (driven by GPIO16). Take the type and surrounding circuit from the WT32-ETH01 reference
design.

### Q7 — RTL8201F `-VB` versus `-VC` footprint

LCSC lists the `-VB-CG` as `QFN-32-EP(5×5)` and the `-VC-CG` as `QFN-32`. Different package/revision codes.
**Compare both mechanical drawings before committing the footprint.** Getting this wrong is a respin.

---

## Blocking — decide before mechanical design

### Q8 — XLR: 3-pin or 5-pin?

5-pin is the DMX512-A standard. 3-pin is what the haunt and small-venue market actually uses. Affects the DMX
module outline and the enclosure. Possibly both variants of the same module PCB.

### Q9 — Enclosure material, and the chassis bonding scheme

Metal is preferred (durability, EMC, and it acts as the antenna counterpoise). But with a metal enclosure:

- Where do the RJ45 shield and any module cable shields bond?
- If the XLR shell and the RJ45 shield both bond to chassis, **domains 1 and 3 are connected** and the
  isolation the design paid for is gone.
- PoE provides no protective earth, so the chassis floats unless deliberately referenced.

This has to be an explicit, documented scheme per enclosure variant, not an accident of assembly.

---

## Blocking — before the mains module exists

### Q10 — Is the mains actuator functional switching or an isolation function?

Decides the component class entirely. Functional switching → relay or contactor. Isolation for safety → a
load-break switch per IEC 60947-3; relays are not suitable. See
[07 — Compliance & Safety](07-compliance-and-safety.md).

### Q11 — Is it safety-related?

If the disconnection is a safety function, EN ISO 13849 / EN 62061 apply, with Performance Level, redundancy
and mirror-contact feedback. **A network-commanded relay is categorically not a safety device.** If the
installation needs an emergency stop, it needs a hardwired one.

### Q12 — How do remote modules connect?

Mains modules cannot stack — creepage, size, enclosure and earthing all forbid it. Two candidates:

1. A short cable carrying 12 V plus a small number of control lines, with isolation provided by the relay's
   own coil-to-contact barrier. I²C over a cable is a poor choice, so this needs different signalling.
2. **The remote actuator gets its own base board.** Consistent with the philosophy already applied to DMX
   ports, costs a switch port and ~$20, and sidesteps the cabled-bus problem entirely.

Option 2 is the current lean.

### Q13 — De-energised state of the mains actuator

Should the load drop out when the node loses PoE? Safer, but "the lights go out because a switch rebooted" is
usually the worse outcome on site. A product decision, not a technical one. Bistable contactors offer a third
answer — and remove ~2 W of standing coil dissipation — at the cost of a harder question about state after a
power failure.

---

## Non-blocking, but decide early

### Q14 — Which EMC standard applies?

Lighting equipment (EN 55015 / EN 61547), professional entertainment (EN 55103-2, now largely EN 55035), or
industrial (EN IEC 61000-6-2 / -6-4)? This determines whether surge appears at the DMX signal port and
therefore whether the PPTC and series resistor footprints get populated. **Settle with the test lab; do not
assume.**

### Q15 — Licence and openness

Not decided. The repository is currently private. Options range from fully open hardware (CERN-OHL) with an
open firmware licence, through open documentation with closed hardware files, to entirely proprietary.

Worth thinking about early: the haunt and escape room community has a strong maker culture, and an open module
interface specification with a permissive licence would let third parties build modules — which is exactly the
ecosystem effect that makes the two-part architecture pay off. That does not require open-sourcing the base
board itself.

### Q16 — Module type ID registry

Who allocates `module_type` values? If third-party modules are ever intended, a range must be reserved for
them, and the descriptor format ([04](04-module-interface.md)) needs a published specification.

### Q17 — Production test strategy

How is a board tested before it ships? At minimum: PoE negotiation, link up, flash read/write, module
connector continuity, and — for DMX modules — an isolation test. Worth designing test points in now rather
than discovering their absence at the first production run.
