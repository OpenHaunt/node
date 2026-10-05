# 10 — Design Decisions

Why each choice was made, and what was rejected. This document exists so settled questions stay settled. If
you are about to reconsider one of these, read the entry first.

**D1 through D15 are from the two-board design.** Several have been superseded or amended by the move to
three boards, and each says so at the top of its entry. A superseded decision is kept rather than deleted,
because the reasoning usually still explains why the replacement looks the way it does.

---

## D1 — Modular architecture rather than a family of complete devices

**Amended by D16.** Originally two boards, base plus module. Now three: mainboard, power board, IO board.
The reasoning below is unchanged and is why the third board was added rather than resisted.

**Why:** certification economics (the base stays a single SELV product with one assessment), board area (no
single board can carry DMX isolation, mains creepage and an LED power stage at once), development velocity (a
module is a fortnight, a product is not), and field repair (swap an €8 module, not a €60 device).

**Consequence:** the interfaces in [06](06-board-interfaces.md) become the most safety-critical document in
the project, because they cannot change once boards are in the field.

---

## D2 — Classic ESP32, not S3 / P4 / C-series

**Superseded by D32.** The mainboard now uses an **ESP32-S3-WROOM-1**, because D20 turned the IO board link
into USB and the classic ESP32 has no USB peripheral of any kind.

**A factual error in this entry needs correcting rather than deleting.** It stated that "ESP32-S31 does not
exist". It does: a dual-core RISC-V SoC with Wi-Fi 6, BLE 5.4, Bluetooth Classic, 802.15.4, a **1000 Mbps
Ethernet MAC** and **USB 2.0 High-Speed OTG**, in mass production, available as `ESP32-S31-WROOM-1` and
`-3`. It is the part this entry's table concluded could not exist, and it is the basis of D33.

The comparison below is kept because it is still the correct reasoning for why the *classic* ESP32 was right
when USB was not a requirement.

**Why:** it is the **only Espressif part with both Wi-Fi and an internal Ethernet MAC**.

| Considered | Wireless | Internal EMAC | Verdict |
|---|---|---|---|
| **ESP32 (classic)** | Wi-Fi 4, BT | **Yes, RMII** | **Selected** |
| ESP32-P4 | **None** | Yes, RMII | Would need a C6 companion for Wi-Fi — two chips, external PSRAM and flash, much higher dissipation |
| ESP32-S3 | Wi-Fi 4, BLE | No | Would need a W5500 — see D3 |
| ESP32-C5 | Dual-band Wi-Fi 6 | No | Dual-band is worthless on a PoE device; only 2 full UARTs |
| ESP32-C6 / C61 | Wi-Fi 6 | No | Too few peripherals |
| ESP32-H4 | **No Wi-Fi** (BLE + 802.15.4) | No | Neither network path — excluded |

*(For the record: "ESP32-S31" and "ESP32-E22" do not exist. E22 is an Ebyte LoRa module family.)*

**Cost of the decision:** 2016-era silicon, Wi-Fi 4, no native USB, and RAM permanently capped at 520 kB —
the PSRAM module variants are unusable because PSRAM occupies GPIO16/17.

**Note on how this was nearly got wrong:** the classic ESP32 was initially rejected on the grounds that UART0
is the console and only two UARTs would remain for three DMX ports. That reasoning was wrong — ESP32 UART
signals route through the GPIO matrix and UART0 can be remapped at runtime while the ROM bootloader still
forces GPIO1/3 in download mode. The point became moot when the port count dropped to one (D7), but the
reasoning is recorded so it is not repeated.

---

## D3 — Internal RMII MAC, not a W5500 over SPI

**Reversed by D32.** The mainboard now uses a **W5500 over SPI**, because no shipping Espressif part has
both an internal Ethernet MAC and a USB host, and the IO board link had to become USB.

**The reasoning below still identifies the real risk**, and it does not go away by being outvoted: SPI
ethernet cannot absorb a large Art-Net broadcast. What changed is the mitigation. sACN with IGMP snooping
was already documented as the preferred protocol; under D32 it stops being a preference and becomes a
requirement for any installation with more than a handful of universes. See D32 and
[08 — Firmware & Network](08-firmware-and-network.md).

**Originally decided:** use the ESP32's internal Ethernet MAC.

**Why:** not throughput of our own traffic — one DMX universe is 210 kbit/s and a W5500 would manage 8–15
Mbit/s comfortably. The reason is **broadcast absorption**. An Art-Net console broadcasting 64 universes puts
13.4 Mbit/s at every node, which must be received and discarded. That is at or beyond the practical ceiling of
SPI Ethernet, and the failure mode is dropped packets and stuttering fades rather than a clean error.

ESP-IDF also drives the W5500 in MACRAW mode — its hardware TCP/IP stack is not used — so the "offload
engine" advantage is illusory.

**Also rejected with it:** the simpler layout (SPI instead of nine impedance-controlled RMII lines) and free
choice of MCU. Worth it.

---

## D4 — RTL8201FI-VC-CG, not LAN8720A

**Moot under D32.** The W5500 integrates MAC and PHY, so there is no separate PHY to choose. The entry is
kept for two reasons: it documents a sourcing trap that recurs, and if the design ever returns to an
internal MAC, the choice comes back.

**Worth recording for that day:** the availability picture has since inverted. At DigiKey the
**LAN8720AI-CP-TR** shows $1.27 and **23,356 in stock** in the industrial grade, while the RTL8201F is not
listed at all. The Realtek part was chosen on LCSC stock; on a DigiKey-sourced BOM the conclusion flips,
and the LAN8720 brings the WT32-ETH01 and Olimex reference designs back into play.

**Originally decided:** Realtek RTL8201FI-VC-CG, LCSC C2763557.

**Originally** the LAN8720A was selected, because both reference designs being copied (WT32-ETH01, Olimex
ESP32-POE) use it and Microchip publishes a complete datasheet. **Changed on availability** — LCSC stock fell
to double digits.

**The `I` suffix is not optional.** It is the industrial grade, −40…+85 °C. The commercial variant
(`RTL8201F-VB-CG`, $0.27 @100, excellent stock) is conventionally 0…+70 °C, and the thermal analysis in
[02](02-system-architecture.md) puts the PHY at a plausible 65–75 °C. The 96-cent premium at 100 pieces is
noise on a BOM containing a $10 PoE module.

**Costs of the change:**

- The reference designs no longer apply to the PHY. Strap options, PHY address and clock direction must be
  validated independently, and Realtek multiplexes straps onto LED and data pins — the classic source of
  "PHY not detected".
- **The footprint must be verified.** LCSC lists `-VB-CG` as `QFN-32-EP(5×5)` and `-VC-CG` only as `QFN-32`.
  `VB` and `VC` are different package codes. Compare both mechanical drawings before layout.

Both are natively supported by ESP-IDF (`esp_eth_phy_new_rtl8201()`).

---

## D5 — Keep the external 50 MHz oscillator with a GPIO16 enable

**Moot under D32.** There is no RMII interface any more, so there is no reference clock to gate and no
strapping conflict to solve. The W5500 runs from its own 25 MHz crystal and talks SPI.

This removes what was, in the RMII design, the single most delicate piece of the mainboard. It also closes
Q6.

**Originally decided,** even though the RTL8201F can generate the RMII clock itself from a 25 MHz crystal.

**Why:** GPIO0 is both the RMII reference clock input and the boot strapping pin. A clock running at reset can
prevent the chip entering download mode. The WT32-ETH01 solves this with an **oscillator enable on GPIO16**,
held off until strapping has been sampled. If the PHY sourced the clock instead, the problem would return and
would have to be solved by holding the PHY in reset — a less proven path.

The oscillator topology works with any RMII PHY. It is the part of the reference design worth keeping even
though the PHY itself changed.

---

## D6 — CA-IS2092**VW**, not the plain W, and not the ADM2587E

**Decided:** Chipanalog CA-IS2092VW, LCSC C7422559.

**Against the ADM2587E** (the industry default): $1.44 versus $7.35 at 100 pieces, with *better* isolation
(3.75 kV rms with VDE/UL/CQC/TÜV approvals versus 2.5 kV rms) and a hand-solderable SOIC-16-WB. The ADI part's
one genuine advantage is **±15 kV IEC 61000-4-2** ESD versus Chipanalog's **±20 kV HBM** — HBM is a much
milder test and the two figures are not comparable. That gap is closed by the SM712 for $0.42, and the
combination ends up better protected than the bare ADM2587E.

**Why the `VW` and not the `W`:** the plain W has ratiometric input thresholds on `/RE`
(V_IH = 0.7 × VCC). At VCC = 5 V that requires 3.5 V, which a 3.3 V MCU cannot produce. The `VW` adds a
separate logic supply pin `VCCL` (2.375–5.5 V), so logic runs at 3.3 V while the isolated DC/DC runs from 5 V.
The ADM2587E offers no equivalent — with it you are forced to run VCC = 3.3 V.

**Also rejected:** the LGA-16 `CA-IS2092A` — marginally cheaper, but leadless (stencil required, X-ray
inspection) and with less creepage on a board that also carries an XLR connector.

---

## D7 — One DMX port, not three

**Decided, and now applies to IO boards generally.** Need a second universe? Hang a second node next to it.
The same argument settled D21, one IO board per mainboard.

**Why:** three bidirectional ports needed three hardware UARTs, three isolated transceivers (~300 mA of
isolated DC/DC alone), three isolation slots, and made the pin budget the binding constraint on MCU selection.
One port collapses all of that.

**Cost:** two switch ports and two PoE allocations instead of one. With classification programming (D13) that
is affordable.

---

## D8 — DMX is an IO board, not a mainboard function

**Decided,** despite the project having started as a DMX node.

**Why:** the mainboard stays small and cheap, and a node that only reads a reed switch does not carry an
isolated RS-485 transceiver and an XLR connector it will never use. Consistent with D1: DMX is one actuator
among many.

**Consequence, since superseded by D19:** the interface originally had to carry a UART with direction
control. With an RP2354 on the IO board it drives its own direction pin and the UART on J3 became a link
rather than a peripheral.

---

## D9 — External data flash, and specifically 16 MB

**Dropped for rev A, and the reason is D19 rather than cost.** The mainboard uses the 16 MB internal flash
of the `ESP32-S3-WROOM-1-N16R2` and carries no separate chip.

**Why the first argument below no longer applies.** The external flash existed because a sector erase
stalls the XIP cache for 30 to 50 ms, and in the two-board design the ESP32 generated DMX timing itself,
where a frame is due every 22.6 ms. **With the RP2354 doing all timing (D19), nothing on the ESP32 is
time-critical.** A 50 ms stall now delays network handling, which is buffered anyway.

The second argument, wear isolation, survives but does not carry the decision on its own: 16 MB with
LittleFS wear levelling is far beyond what recording and logging will consume in this application.

**Reinstate it** if the ESP32 ever regains a hard real-time job, or if recording volumes grow enough to
threaten the boot medium. The footprint is worth keeping in the layout if space allows.

**Originally decided:** W25Q128JVSIQ, 16 MB, LCSC C97521.

**Why external flash at all** — two reasons, neither of which is capacity:

1. **Writing to it does not stall the XIP cache.** The internal flash shares the SPI0/1 controller the CPU
   fetches code through; a 4 kB sector erase stalls execution for 30–50 ms, which collides with a DMX frame
   every 22.6 ms. A chip on SPI2/SPI3 is an ordinary peripheral transaction.
2. **Wear is isolated from the boot medium.** A worn-out data flash costs the recording feature, not the
   device.

**Why 16 MB and not more:** 2²⁴ = 16,777,216 bytes exactly — 16 MB is the largest capacity reachable with
standard 3-byte SPI addressing. Above it you must enable 4-byte address mode, and the W25Q256JV's
**non-volatile ADP bit** in status register 3 means a reset while the chip is in 4-byte mode, with a driver
assuming 3-byte, produces **silently shifted reads rather than an error**. Not worth it for capacity nobody
needs.

**Rejected:**

| Part | Size | $/100 | Why not |
|---|---|---|---|
| W25Q256JVFIQ | 32 MB | $3.52 | 4-byte addressing; SOIC-16-300mil (3× the area); only ~500 in stock |
| GD25Q256EYIGR | 32 MB | $2.81 | Better than the Winbond 32 MB (WSON-8, cheaper, more stock) but still 4-byte addressing |
| W25Q512JVFIQ | 64 MB | $12.98 | Price cliff — more than the rest of the logic section combined |

Capacity check: 16 MB gives ~45 s per free megabyte of raw DMX capture, and endurance of ~1.68 TB total
writes. With event-based rather than frame-based storage, effectively unlimited for this application.

---

## D10 — ESP32 module with 4 MB flash (`-N4`)

**Decided,** having initially recommended `-N8`.

**Why the change:** with the web UI assets on the external flash, the internal flash holds only code, giving
~1.95 MB per OTA slot. A firmware with Wi-Fi, Ethernet, lwIP, HTTP server, mDNS, Art-Net, sACN and the module
abstraction lands realistically at 1.1–1.4 MB — 30–40 % headroom.

`-N8` is footprint- and pin-identical (+$0.69 @100) and remains a drop-in if that turns out optimistic.
`-N16` buys nothing: firmware will not grow to 6 MB, and the real ceiling is RAM, not flash.

**Absolute rule: no `R` suffix.** `-N4R2` and siblings carry 2 MB PSRAM which occupies **GPIO16 and GPIO17** —
the oscillator enable and the module UART TX.

---

## D11 — Take the highest rail from the PD module and step down

**Decided.** Silvertel **Ag9712-2BR**, 12 V / 1.0 A / 12 W, replacing the earlier Ag9705-2BR at 5 V / 9 W.
Now the `PWR-AF` variant rather than the only power stage, per D16.

**Why:** 12 V peripherals are needed — relay and contactor coils in particular. Deriving 12 V by boosting from
5 V at these currents would be poor engineering; taking the highest rail from the isolated module and stepping
down is correct.

Ag9700 family output options: 3.3 V / 1.8 A, 5 V / 1.8 A, **12 V / 1.0 A**, 24 V / 0.5 A (FL only). The
`-2BR` suffix brings the bridge rectifiers on-board, covering both Mode A and Mode B.

---

## D12 — Two independent bucks, not a cascade

**Decided:** 12 V → 5 V and 12 V → 3.3 V as separate converters.

**Why:** cascading 12→5→3.3 multiplies efficiencies (0.92 × 0.92 = 0.85) and couples the rails.

**Parts:** AP63205WU-7 (5 V) and AP63203WU-7 (3.3 V). Both 3.8–32 V in, 2 A, TSOT-26.

**Amended by D36.** The 3.3 V converter is no longer on the power board, and the AP63203 is not used
anywhere: the mainboard regulates with an AMS1117 LDO. The "not a cascade" argument still holds for
12 V → 5 V and for any future rail.

**Chosen for EMC, not price.** Both feature **Frequency Spread Spectrum** (±6 % jitter). The board already
carries the PoE module's switcher and, on DMX modules, an isolated DC/DC. A cheaper MP2315 or TPS562201 would
have added two more fixed-tone 1.1 MHz emitters. The 32 V input rating also gives comfortable margin over the
regulated 12 V rail.

---

## D13 — 802.3af, with classification programming strongly recommended

**Superseded by D16 and D17 on the standard, upheld on classification.** The range now spans af to bt
Type 4, and classification programming matters more than ever, because a range that mixes 85 W and 3 W
nodes on one switch makes the reservation arithmetic worse rather than better.

**Was:** sufficient for the base board (~1.3 W) plus typical modules. Insufficient if a module needs a long WS2812
run or a large contactor — and moving to 802.3at later is a **base board respin**, not a component swap:
different module, different magjack rating, different thermal design.

**The classification point:** the Ag9700 presents a **Class 0 signature by default**, making the switch
reserve **15.4 W per port** for a device drawing 1.3–4 W. On a 370 W switch that is roughly 24 nodes instead
of 90. For a product whose scaling story is "hang another one next to it", the **-FL** variant with
programmable classification is worth the premium.

**Correction, 2026-09-07: classification programming is not available with the parts D29 selected.** All
three SMT families are fixed-class, and none of them has an RCLASS pin:

| Module | Class |
|---|---|
| Ag9900-MTB | fixed **Class 0** — 5 pins, no class pin at all |
| Ag5700-LPB | fixed Type 2 **Class 4**, requests 30 W |
| Ag59600-LPB | fixed Type 3 **Class 6**, requests 60 W |
| Ag59800-LPB | fixed Type 4 **Class 8**, requests 90 W |

The programmable-class part is the Ag9700, which is the through-hole SIL module D29 retired. So this
entry's own worst case is now designed in: a 3 W `PWR-AF` node reserves 15.4 W. It is worse at the top —
`PWR-BT3` reserves 60 W per port, six ports on a 370 W switch — which makes the high variants something you
fit **because** the load needs them, never a default.

**What replaces it:** the bt modules carry `TYP2-DET` and `TYP3-DET` outputs, so the node can report which
PSE type it actually got and derate its own budget at runtime. That is the opposite direction from
classification programming — it does not reduce what the switch reserves — but it does make the mismatch
visible instead of silent. See D35 and `06`.

---

## D14 — SM712 always fitted; PPTC and series resistors as unpopulated footprints

**Decided.**

**SM712-02HTG** is always fitted: asymmetric −7 V / +12 V standoff matching the RS-485 common-mode range,
±30 kV IEC 61000-4-2, $0.42. At that price there is no case for leaving an XLR connector unprotected.

**PPTC and 10 Ω series resistors are footprints with 0 Ω fitted.** Surge (IEC 61000-4-5) is normally not
applied to signal ports; it only appears if the product is assessed under EN IEC 61000-6-2 and the >30 m line
rule bites. Deferring the decision costs two footprints and keeps the full driver swing in the default build.

**If fitted:** Bourns **MF-MSMF010-2** — the **60 V** rating is the whole point, because XLR-3 is also the
audio connector and 48 V phantom power will eventually be plugged in. The commonly recommended
`MF-PSMF010X-2` holds off only 15 V and is the wrong part. Series limiting is done by 10 Ω **pulse-withstanding
resistors** (ERJ-P06F10R0V), not by the PPTC — a PPTC takes ~1.5 s to trip and contributes only its 0.7 Ω
during an 8/20 µs event.

---

## D15 — Metal enclosure with an external antenna

**Superseded by D22.** The design moved to the PCB-antenna `ESP32-WROOM-32E`, which removes the antenna,
the pigtail, the bulkhead, the enclosure penetration and the gain compliance argument. Kept because the
constraint below comes straight back if anyone proposes an external antenna.

**Was:** Taoglas **GW.26.0151**, RP-SMA(M), 1.8 dBi, IP65, with a Taoglas **CAB.774** U.FL to RP-SMA(F)
bulkhead pigtail.

**Constraint:** the WROOM-32UE modular approval covers monopole antennas up to **2.33 dBi**. See
[09](09-compliance-and-safety.md) — this rules out most of the market, including the mechanically ideal
articulated Linx ANT-2.4-CW-HWR at 3.2 dBi.

**The metal enclosure is an advantage here**, not a handicap: a whip is a monopole and needs a counterpoise.
The bulkhead bonds to the enclosure, which becomes the ground plane. It must bond to **bare metal** — not
through powder coating or anodising.

**Open:** whether Wi-Fi is needed at all. The device is PoE-powered and therefore always has an Ethernet
cable. Dropping Wi-Fi removes the antenna, the pigtail, the bulkhead, the enclosure penetration, the strain
relief and the antenna compliance argument, and allows the better-stocked `ESP32-WROOM-32E-N4`. See
[11](11-open-questions.md).

---

## D16 — Three boards: mainboard, power board, IO board

**Decided.** The two-board design becomes three, splitting the power stage onto its own board.

**Why:** PoE class is a property of the effect, not of the node. A reed switch needs 2 W and a pixel run
needs 60 W, and in the two-board design those were different products because moving from 802.3af to
802.3at was a base board respin: different PD module footprint, different magjack rating, different thermal
design. Splitting power out makes class a stocking decision.

**Cost:** one more connector pair, one more board to assemble, and an isolation domain that now spans two
boards, which is the single most delicate consequence of the whole change. See D17 and
[09 — Compliance & Safety](09-compliance-and-safety.md).

**Rejected:** population options on one board. The Ag9712 and the Ag5800 have different footprints,
different pinouts and a 2:1 area ratio. A board that could take either would be a bad version of both.

---

## D17 — The etherCON and magnetics live on the mainboard; four taps cross to the power board

**Decided.** The mainboard owns the network connector, the magnetics, and every impedance-controlled trace.
The power board receives four DC conductors.

**Why:** the magjack and the PHY have to be close together, and the traces between them are the only
controlled-impedance content in the node. Putting the connector on the power board would mean routing 100 Ω
differential pairs across a board-to-board connector and controlling impedance on two PCBs. This way the
power board is a two-layer board with no high-speed content at all.

**The cost:** the magnetics now live on the board you cannot swap, so they set the ceiling for the whole
range. D28 resolves that by matching the etherCON's own Type 4 Class 8 rating, so the mainboard front end
is **Type 4 end to end** and the ceiling moves off the mainboard entirely.

**Consequence:** un-isolated 37 to 57 V now crosses a connector. J1 is a separate 4-pin connector with a
keep-out on both boards, not four pins on the main connector, so the barrier is visible on an assembled
board rather than being a pin-numbering convention.

**A clearance claim in an earlier draft of this entry was wrong** and is worth correcting rather than
quietly deleting. It said J1's 2.54 mm pitch was thin for 1500 V. It is not: all four taps are cable-side
conductors within ±57 V of each other, and in a hipot test they are tied together and driven as one node
against Domain 2, so J1's own pin-to-pin potential is zero during the test. **J1 needs 57 V pin-to-pin and
1500 V to Domain 2**, and only the second is a clearance problem. The keep-out answers it.

**Watch the current figure.** Each tap carries the full pairset current, **960 mA** at Type 4, not a
quarter of the total. Within a pair that splits into 300 mA per wire. Both numbers are right and they
answer different questions.

---

## D18 — One power board envelope, sized for the largest module

**Decided.** Every power board has the same outline and mounting pattern, sized around the largest PD
module in the range. On a `PWR-AF` most of that area is empty.

**Largely superseded by D31.** With the boards coplanar rather than stacked, a shared outline is no longer
needed: only the mating edge and its mounting holes have to line up. A small `PWR-AF` and a large `PWR-BT4`
can now coexist. The reasoning below is kept because it explains what the common envelope was buying, and
why losing the stack made it unnecessary rather than merely optional.

**Also amended by D28:** the envelope, where it still applies, is sized for the 40 × 25 × 15.8 mm
AG596xx-LPB rather than the 70 × 35 mm Ag5800.

**Amended again by D35.** With the IO board across the back of both other boards, `J3` has to line up with
`J4`, so the power board's **width is fixed** even though its depth is free. A `PWR-AF` may be shallow but
not narrow. That is less freedom than D31 promised and more than D18 originally allowed.

**Why:** one mainboard outline, one enclosure, one stack height, and a class change that is genuinely only
a board swap. Two envelopes would mean two enclosures and a modularity story that only holds within a size
class, which is most of the value gone.

**Cost:** wasted board area on the cheap variant, which at these panel prices is a rounding error against
a second enclosure tool.

---

## D19 — Every IO board carries an RP2354

**Decided.** RP2354A for most boards, RP2354B where the pin count demands it. The IO board stops being
passive silicon on a bus and becomes a peer with its own firmware.

**Why, in order of how much each one matters:**

The interface stops leaking the host's constraints. Most of the old module interface document was rules
about ESP32 strapping pins, because a third-party board that pulled the wrong line could prevent the node
booting. A link carrying a serial protocol cannot do that, and that makes a published third-party interface
a much safer thing to offer.

Timing stops being shared. The ESP32 can stall 50 ms on a flash erase, absorb a burst of broadcast Art-Net,
or reboot for OTA, and the outputs hold. On the old design a wedged ESP32 was a wedged effect.

One fast line becomes twelve PIO state machines. `MOD_FAST0` was a single RMT channel, so one WS2812 string
or one bit-banged protocol per node. DMX break timing, WS2812, encoders, servos and one-wire sensors all
become concurrent local programs.

Inputs get timestamped on the board that saw the edge, which is a design rule the old architecture stated
and could not really enforce.

**Cost, and it is not small:** a second firmware image, a second build, a second update path, a link
protocol to specify and version, and a resident bootloader that is now the highest-risk firmware in the
project. Accepted.

**Why the RP2354 and not the RP2350 plus a flash chip:** removes a part, a footprint, four signals and the
QSPI layout constraints from every board in the catalogue. The 2 MB of stacked flash is a W25Q16JVWI die,
and the pinout is identical to the flashless part, so falling back is possible if 2 MB ever runs out.

---

## D20 — The link is USB from the start

**Amended by D32: USB now, not later.** J3 pins 7 and 8 carry a USB full-speed differential pair from
rev A.

**Why the change:** the IO boards are the growing half of the product, and their interface is the thing that
must not break. A UART link now and USB in generation 2 would mean two incompatible IO board families in a
catalogue that is meant to grow. The mainboard gets rebuilt for generation 2 anyway; the IO boards should
not have to be.

**A detail that makes this cleaner than it sounds:** the RP2354 is USB 1.1 full-speed and nothing else. The
ESP32-S3 host is full-speed too, so nothing is lost today, and in generation 2 the high-speed S31 host talks
to the same full-speed device through a hub. The IO board interface is genuinely identical across
generations rather than merely similar.

**Originally decided:** a framed UART protocol today, laid out so the same two pins could carry USB later.

**Why not USB now:** the classic ESP32 has no USB peripheral. Not host, not device, none. It is 2016
silicon and USB arrived with the S2. Every Espressif part that has USB lacks either Wi-Fi or the internal
ethernet MAC, so going to USB today means either an ESP32-P4 with a C6 companion for Wi-Fi, or an external
USB host controller such as a MAX3421E over SPI at 12 Mbit/s with a heavy software stack.

**Why not UART forever:** USB gives enumeration, a standard descriptor mechanism, much more bandwidth, and
the RP2350's ROM bootloader as a firmware recovery path. That last one matters more than the bandwidth.

**Cost of deferring:** two footprints and a differential-pair routing constraint that costs nothing today.
**What is unresolved** is whether the mainboard ever gets a part that can be a USB host, which is Q3 in
[11 — Open Questions](11-open-questions.md).

---

## D21 — One IO board per mainboard

**Decided.** One mainboard, one power board, one IO board, point to point.

**Why:** the same argument as D7. Need a second bank of inputs, or a second universe? Hang another node
next to it. Multiple IO boards would need addressed framing now and a USB hub later, plus a slot-identity
scheme so the firmware knows which board is where.

**Note that the old argument for this is gone.** In the af design, power budget made multiple boards
impractical on its own. At 85 W that constraint disappeared, and the decision now rests entirely on keeping
the link simple. That is a weaker foundation, so if a real use case for two IO boards appears, this is the
decision to revisit first.

---

## D22 — PCB antenna module, no external antenna

**Still holds, now via the `ESP32-S3-WROOM-1`.** The part changed under D32 but the reasoning did not: a
module with an integrated PCB antenna, no U.FL, no pigtail, no bulkhead, no gain compliance argument.

**Q2 is now answered: the node does not use Wi-Fi.** The radio on the S3 is present and unused. It costs
nothing extra, the module approval covers it, and BLE remains available should a maintenance channel ever
be wanted. See D33 for where wireless does belong.

**Originally decided,** replacing the `-32UE` and the external antenna of D15.

**Why:** with three boards to place, an etherCON fixing one face of the enclosure, and up to 85 W of heat
to get out of the box, an external antenna with a U.FL pigtail, a bulkhead penetration, strain relief and
a gain compliance argument is a cost the design does not need. The `-32E` has roughly 14× the LCSC stock of
the `-32UE`, saves about $7 per unit in antenna BOM, and carries its modular approval with no antenna
argument at all.

**Cost:** Wi-Fi range inside a metal enclosure, which is poor to nonexistent. That is acceptable because
Wi-Fi was never the primary transport, only an emergency access path, and whether it is needed at all is
still Q2. If Wi-Fi is dropped entirely, nothing on the board changes.

---

## D23 — Per-rail hardware eFuse, not firmware current limiting

**Decided.** Every rail leaves the power board through a current-limiting load switch with a hard trip.

**Why:** at 12.95 W a shorted rail is a brownout. At 85 W it is a fire, and the PD module's own protection
sits at the 48 V input where it will deliver 85 W into a shorted 3.3 V rail without noticing. The trip must
not depend on the ESP32 being alive, because the ESP32 is powered from one of the rails being protected.

**Three things come free** and are worth having in their own right: defined inrush so an IO board with bulk
capacitance does not collapse the rail for everything else, the ability to power-cycle a locked-up IO board
without power-cycling the node, and sequenced rail startup.

**Rejected:** metering plus a firmware shutdown. It is the same protection with a slower, less reliable
actuator that shares a failure mode with the thing it is protecting against.

---

## D24 — INA3221 for the low rails, INA226 for 24 V

**Decided.** INA3221 at `0x40` covers 12 V, 5 V and 3.3 V. INA226 at `0x44` covers 24 V.

**Amended by D36.** With no 3.3 V rail, the third INA3221 channel meters the mainboard's unconditional 5 V
feed, so the node's own consumption is visible separately from the IO board's.

**Why not one INA3221 for everything:** its common-mode range tops out at **26 V**. A 24 V rail with
switching ripple and load transients has no usable margin against that, and the failure is a measurement
that quietly goes wrong rather than a part that visibly dies. The INA226 is rated to 36 V.

**Why the 48 V PoE input is not metered:** it is in Domain 1, so its I2C would have to cross the isolation
barrier, and the entire architecture rests on nothing crossing it. Input power is inferred from the output
rails and the module's rated efficiency, which is close enough for a health topic and a budget check.

---

## D25 — Power board identified by I2C EEPROM, not strap pins

**Decided.** 24AA02UID at `0x50`, on the bus the metering already needs.

**Why:** it carries what the mainboard cannot work out for itself, which is more than an identity. Which
rails exist, each rail's eFuse limit, the shunt calibration constants the metering needs, and the class
declared to the PSE. Plus a factory-programmed unique serial, so every power board is traceable with no
programming step in production, at about $0.15 on a bus that exists anyway.

**Rejected: strap pins.** The argument for them is that they read before I2C is up. That turns out to be
worthless, because the mainboard has nothing useful to do in that window: it cannot enable a rail until it
knows the budget, and it does not know the budget until it has read the descriptor. Straps also carry no
serial, no calibration, and cost connector pins.

**Rejected: an ID resistor to an ADC.** Eight to sixteen distinguishable variants, no serial, no
calibration data, and the ESP32's ADC is not a part of that chip anyone speaks well of.

---

## D26 — Neutrik NE8FAH-LR-DAE, and therefore discrete magnetics

**Decided.** etherCON A-series chassis receptacle, horizontal PCB mount, with the Halo light ring.

**Why etherCON:** it locks, it survives being trodden on, and it is what this market already has in its
cable stock. A standard RJ45 plug still mates with it, so nothing is lost for a fixed install. It is rated
**PoE Type 4 Class 8, 100 W**, at 1.5 A per contact against the 480 mA per conductor Type 4 actually needs,
so the connector never becomes the limit.

**The consequence that matters: it has no magnetics.** Neutrik do sell magnetics variants,
`NE8FAH-M-Mb-LR-DAE` and `NE8FAH-M-Gb-LR-DAE`, and their datasheets state twice over that they are
**strictly not intended for use in PoE systems**. They are not an option. The magnetics become a separate
part on the mainboard, chosen in D28.

That is an improvement rather than a compromise. The transformer and the connector become independent
choices, and the transformer can be selected purely on its DC bias rating without also having to be the
right mechanical part.

**Two things it buys that were not the reason for choosing it.** The shield is isolated from panel ground
and arrives on a PCB pin, so shield bonding becomes a documented decision on the schematic instead of a
consequence of how the connector was mounted. And the Halo ring is an externally visible indicator at the
connector, which is exactly what `/api/v1/identify` needed and never had.

**The ring contains no LEDs**, which turned out to be an advantage. You fit two 0603 LEDs on the mainboard,
left and right, so both the colours and the two-channel behaviour are design choices rather than whatever
Neutrik happened to fit. The constraint that comes with it is a **0.80 mm maximum height**, which excludes
much of the 0603 market; Würth's `150060xS75000` series sits exactly on the limit. Q8 in
[11](11-open-questions.md) has the detail.

**Two costs that only appeared on reading the full datasheet.** The connector is rated **−30 to +80 °C**,
which makes it the lowest-rated part in the node and the real thermal limit rather than the silicon. And it
**does not intermate with Neutrik's own CAT6 cable connectors**, `NE8MC6-MO` and `NKE6S*` cables, which in a
venue full of crew-owned etherCON stock is a support problem worth documenting in the product manual.

---

## D27 — Phase 1 sockets a WT32-ETH01 and accepts a second RJ45

**Abandoned.** There is no socketed phase. Rev A carries the ESP32-S3 and W5500 directly, with one etherCON
and no patch lead.

**Why it collapsed:** the whole construction existed to avoid designing an ESP32 plus RMII PHY section
before anything else could be tested. Under D32 there is no RMII PHY, the W5500 is an SPI peripheral, and
the thing the socket was protecting against no longer exists. What remained was a second RJ45, a patch lead
inside the enclosure, a module with five free pins, no external flash and only one Halo LED — all cost, no
benefit.

**Kept as a record** because the second-jack analysis was correct and would apply again to any socketed
module: the WT32-ETH01 owns its RJ45 and does not bring the differential pairs out.

**Originally decided:** rev A sockets a WT32-ETH01 and carries a plain second RJ45 with a 10 cm patch lead.

**Why a phase 1 at all:** the ESP32 plus RMII PHY section is the fiddliest part of the mainboard and the
part with the best-known reference design. Socketing it means rev A tests what is actually new, which is
the power board, the isolation split, the link and the IO board.

**Why the second jack:** the WT32-ETH01 has its own RJ45 and does not bring the differential pairs out to
its header. Its 28 pins are GPIO and power only. So the mainboard's magjack cannot feed it, and its own
jack cannot yield PoE taps because it is a cheap non-PoE part with the centre taps terminated internally.
Two sets of magnetics in series over 10 cm costs a few dB at 100BASE-TX and nothing else.

**Rejected: going straight to phase 2.** Tempting, and it was my recommendation, but it front-loads all the
risk into one board spin.

**Phase 1 is a deliberate throwaway.** Draw it fast, learn from it, then start phase 2 as a fresh schematic
rather than editing phase 1 into shape. The alternative, one KiCad project carrying both as DNP variants,
was rejected: the phases differ in more than population, and making the shared 80 % harder to read in order
to avoid redrawing it once is a bad trade.

**Rejected: a socketed module with PoE on board**, such as an Olimex ESP32-POE-ISO. It bundles its own
power stage, which is precisely the thing the power board exists to own, so phase 1 would not prove it.

**Two limitations follow and both are acceptable.** Phase 1 has no external flash, because its four pins
collide with what the module has already spoken for, so no web UI assets, no recording and no served
RP2354 images. And `HALO_LED` sits on a different GPIO in each phase, so keep it behind one build-time
definition.

**Mark the second jack as phase-1-only in silkscreen** so nobody mistakes it for a second network port.

---

## D28 — Würth 7490220122 magnetics, Type 4 front end

**Decided.** Würth **7490220122**, from the WE-LAN 1000BASE-T with 4PPoE family.

| Property | Figure |
|---|---|
| Compliance | **"Compliant with IEEE 802.3bt for 4PPoE"**, stated on the datasheet |
| **DC bias** | **1000 mA per centre tap**, pins 13–24 only |
| Open-circuit inductance | **350 µH** min |
| Insulation test voltage | 1500 V RMS, 1 min |
| Data rate | 10/100/1000BASE-T, 4 channels, 1:1 |
| Temperature | −40 to +105 °C |
| Package | 24-pin SMD, 17.55 × 14.7 × 8.8 mm |
| Price / stock | $6.98, 18,748 at DigiKey |

**1000 mA covers Type 4's 960 mA per pairset**, so the transformer matches the etherCON's own Type 4
Class 8 rating and the mainboard stops being the limit on anything. What class a node actually runs at
becomes purely a power board decision, which is what the three-board split was for.

### Why not the cheaper parts in the same family

The family is a clean ladder and two of the four are traps for this design.

| Part | OCL | Bias | Verdict |
|---|---|---|---|
| 7490220120 | 350 µH | 350 mA | af only |
| 7490220121 | 350 µH | 600 mA | Type 3 only. Caps the range at 51 W to save $0.51 |
| **7490220122** | **350 µH** | **1000 mA** | **Selected** |
| 7490220123 | **180 µH** | 1500 mA | **More bias, worse inductance.** See below |

**The 7490220123 is the trap.** It has the highest bias rating in the family and I recommended it before
reading the datasheet, on stock and price. Its 350 µH siblings are the right parts: **180 µH is a
gigabit-optimised value and thin for 10BASE-T**, which the RTL8201FI can negotiate down to and which needs
the higher open-circuit inductance to meet its droop requirement. More DC headroom is worthless if the link
degrades when a switch negotiates 10 Mbit.

### Why not the Halo TG110-HPE7N5LF

It was selected first, on a DigiKey parametric figure of 0.6 A, and the datasheet does not support it.

- Its datasheet claims **"IEEE802.3af/at Compliant"** and says nothing about bt, which is unsurprising for
  a 2016 document.
- **It states no DC bias current anywhere.** Only return loss, insertion loss and a 1500 V hi-pot. The
  0.6 A came from DigiKey's parametric field, not from Halo.
- It is rated to +85 °C rather than +105 °C, on a design with a real thermal problem.
- 515 in stock against 18,748.

The reasoning built on that 0.6 A was mine, not the manufacturer's: at is 2-pair and bt Type 3 is 4-pair at
the same per-pairset current, so an at-rated transformer should carry Type 3. That is probably true, and
"probably true, with no stated number and no claimed standard" is the wrong footing for the part that fixes
the whole product's power ceiling on the board that must not be respun.

Its advantages were real and are worth recording: $3.72 against $6.98, and 12.70 × 7.11 mm against
17.55 × 14.7 mm. Half the price and a third of the area. Not enough.

### Layout constraints this part brings

**The bias rating applies to pins 13–24 only**, the X-side centre taps. That side must face the etherCON.
Fitting it the other way round puts PoE current through a winding not rated to carry it, and the datasheet
is explicit about the restriction.

**Four channels, two unused.** Pairs 1/2 and 3/6 use channels 1 and 2. **Pairs 4/5 and 7/8 must bypass the
transformer**: tie 4+5 and 7+8 and run them straight to J1.

An earlier draft of this entry said the spare pairs could optionally pass through channels 3 and 4 "for the
common-mode filtering". That is wrong and would destroy the part. Centre-tap injection relies on the two
conductors of a pair sitting at the same DC potential so the winding halves cancel; the tied spare pairs do
not, so wiring them across a winding puts up to 960 mA straight through it as a shorted turn. At
1000BASE-T all four pairs carry data and all four use centre-tap injection, which is why the part has four
channels. On a 10/100 PHY two have no job.

The part is also gigabit-capable, which buys nothing today because the ESP32's EMAC is 10/100 and will
stay 10/100. It is simply how bt-rated magnetics are sold.

## D29 — Silvertel SMT `MTB` / `LPB` families, output voltage chosen per variant

**Decided.** Every power board uses a surface-mount Silvertel module. `MTB` at af, `LPB` at at and bt.

| Variant | 12 V | 24 V | Standard | **Design for** | Peak | Size | Eff |
|---|---|---|---|---|---|---|---|
| `PWR-AF` | AG9912-MTB | AG9924-MTB | af | **12 W @70 °C** | — | 21 × 14 × 13 | 87 % |
| `PWR-AT` | AG5712-LPB | AG5724-LPB | at | **24 W** | 30 W | 33 × 22 × 13 | 88.5 % |
| `PWR-BT3` | AG59612-LPB | AG59624-LPB | bt Type 3 | **48 W** | 60 W | 40 × 25 × 15.8 | 93 to 94 % |
| `PWR-BT4` | AG59812-LPB | AG59824-LPB | bt Type 4 | **67 W** | 100 W | 50.6 × 30 × 14.7 | 94 to 95 % |

**"Design for" is Silvertel's own figure, and it is below both the module rating and the standard's PD
budget.** An earlier version of this table carried the module ratings (30 / 51 / 71.3 W) and was optimistic
by up to 4 W per variant. The datasheets say: the Ag5700 is "24W continuous, 30W peak"; the Ag59600
"should be designed for a maximum continuous output power of 48W"; the Ag59800 "be designed for a maximum
continuous output power of 67W" wherever cabling and PSE cannot be specified — which in a haunt is always.

**90 to 100 W is not obtainable over PoE.** 90 W is what a Type 4 PSE puts on the cable, 71.3 W survives to
the PD, and 67 W is what Silvertel will stand behind. The 100 W on the AG59812/24 label is the converter's
rating. A node that genuinely needs 90 W is a DC-input power board, not a PoE one.

**The af module derates with temperature, and only at 12 V.** Ag9912-MTB drops 12 W → **9 W** between 70
and 85 °C; Ag9924-MTB holds 12 W flat. In a sealed enclosure that is not hypothetical, and it is an
argument for 24 V even on the smallest variant.

**All four families are fixed-class.** See the correction in D13.

**This closes the old "SIL or SMT?" question outright.** The two-board design was built around the
Ag9712-2BR, a 56 × 18 × 14 mm through-hole SIL module that defined the board outline and had to be
hand-fitted. The SMT families are a third of the area, machine-placeable, and available in tape and reel
across the whole power range.

**The output voltage is a per-variant implementation detail, not a range-wide standard.** Each board picks
whatever module suits it, does its own down-conversion, and declares the resulting rails and per-rail
budgets in its descriptor. Firmware reads `rails_present` and the `limit_mA_*` fields and enforces them
without knowing or caring what topology produced them. A `PWR-BT` on a 24 V module has a 24 V rail; one on
a 12 V module does not. Both are legitimate.

**Efficiency decides where the heat lands, not how much there is.** A fully loaded Type 4 node pulls
71.3 W through the enclosure and all of it becomes heat. What 94 % buys is that only 4.4 W lands on the
power board, on a part lying flat, rather than the 10.5 W an earlier draft put into a DIL Ag5800 standing
proud of it. The rest spreads across the IO board.

**Sourcing note, re-checked 2026-09-07.** AG9912-MTB 2018, AG9924-MTB 2188, AG5712-LPB 1310, AG5724-LPB
959, AG59612-LPB 288, **AG59624-LPB 0** (8 the day before), **AG59812-LPB 0**, AG59824-LPB 278. Both
unavailable parts are bt, and they sit on opposite voltages, so **Type 4 is only buildable at 24 V**.

**The bridge rectifiers are not inside the module.** `04` claimed they were. The Ag59800 block diagram and
its Figure 15 show `BR1` and `BR2` as discrete parts ahead of `VIN+`/`VIN−`, and the pin description says
the pins "connect to the output of the POE input bridge rectifiers". J1 keeps its polarity freedom, but two
bridges are components on the power board. The other families use the same wording and need checking
individually.

**Ag5800 and Ag6800 both appear in earlier drafts and neither should be used.** Ag5800 is the through-hole
predecessor. Ag6800 is a PSE module, the thing in the switch.

---

## D30 — `CHASSIS` is its own net, bonded to `GND` through a population option

**Decided.** The etherCON shield and the mounting holes sit on a `CHASSIS` net. It reaches `GND` only
through a fitted **1 nF / 2 kV in parallel with 1 MΩ**, with a **0 Ω footprint** alongside for a hard bond.

**Why this came up:** the mainboard's mounting holes are `MountingHole_Pad`, so they are electrically live.
Left on `GND`, as is the default in most designs, they bond a metal enclosure to logic ground through four
screws. That is the enclosure trap from [02](02-system-architecture.md) arrived at by accident while doing
something else entirely, and it is invisible on the schematic because it looks like ordinary grounding.

**What each part does.** The capacitor gives ESD and HF a low-impedance path out of the enclosure. The
resistor stops the chassis floating up statically without allowing DC circulating current. The 0 Ω is there
for the case where EMC testing shows a hard bond performs better.

**Why not just decide now:** because the answer depends on the enclosure, the installation and the test
lab, and none of those exist yet. Three footprints keep it open. Committing now would mean committing on a
guess and finding out at the worst possible moment.

**The etherCON makes this possible.** The NE8FAH-LR-DAE is isolated from panel ground and brings the shield
out on a PCB pin. A connector that bonds its shield to the panel would have made the decision mechanically,
before the schematic got a say.

**Still open:** which option to fit, which is a test result, and what an IO board does with its own cable
shields. See Q9 in [11](11-open-questions.md).

---

## D31 — Coplanar boards with right-angle connectors, not a stack

**Decided, and still the basis of the mechanics. The arrangement *within* the plane was changed by D35** —
the boards are no longer in a row. Everything below about coplanar rather than stacked stands unchanged;
what D35 replaces is the ordering, and with it the six signals this entry hands to the power board.

**Was: mainboard, power board, IO board, in a row.**

```
   MAINBOARD ═══ POWER BOARD ═══ IO BOARD
   etherCON                      XLR, terminals
   (outward)                     (outward)
```

**Two reasons, and the second one is the stronger.**

**Rails go straight from producer to consumer.** With the power board in the middle, the IO board's supply
never crosses the mainboard. That matters more than it sounds: a WS2812 IO board drawing 40 W at 5 V pulls
**8 A**, and in a mainboard-in-the-middle arrangement that current would cross two connectors and the
whole mainboard. Two 2.54 mm contacts are rated 3 A each, so it was already over budget before anything
else was connected.

**Both boards with outward-facing connectors end up at an edge.** The mainboard carries the etherCON, the
IO board its application connectors, and the board that needs nothing from outside sits between them. In
the earlier order the mainboard was in the middle while its main connector had to reach the enclosure
wall.

**What it costs: the power board passes six signals through** that are none of its business — `USB_D+/−`,
`IO_nRESET`, `IO_BOOTSEL`, `IO_ATTN`. USB is the delicate one: a differential pair across two extra
connectors and a board carrying several switching regulators. At **full speed** that is acceptable, with
conditions: route it as a 90 Ω pair, keep it away from the regulators and their inductors, unbroken ground
plane beneath, and as short as the board allows.

**This needs re-examining for generation 2.** The S31 has high-speed USB at 480 Mbit/s, where two
connectors and a board crossing are no longer harmless. Generation 2 brings the backplane anyway, and with
it a different topology.

**Consequences for the connectors:** `J3` moves from the mainboard to the power board. `J20`, the SWD
passthrough, disappears entirely — with the IO board at an edge it is directly accessible, so the SWD
header belongs on the IO board next to the RP2354 rather than routed through two other boards.

**Why, in order of how much each one matters:**

**Thermal access.** Stacked, the hottest board is the power board, and it ends up in the middle wrapped by
the other two. Coplanar, all three lie flat and couple directly into the enclosure floor. Q9 was the
hardest remaining question in the project and this turns it into an ordinary design task.

**Board outlines become independent.** Stacking wanted one envelope and one stack height, which is what
D18 was for. Coplanar, only the position of the connector on the mating edge has to match. A `PWR-AF` may
be small and a `PWR-BT4` large, instead of both being sized for the larger module.

**Everything is probeable during bring-up.** No separating a stack to reach the middle board.

**Phase 1 stops distorting the mechanics.** A socketed WT32-ETH01 no longer sets the stack height for the
entire prototype.

**What it costs:**

**Floor area.** Three boards side by side take more space than three stacked. The enclosure becomes flat
and wide rather than compact and tall, which for a box screwed to a maze ceiling is arguably the better
shape, but it is a real change to the product's proportions.

**The connector carries no mechanical load.** Standoffs used to hold a stack together and take cable
strain. In the plane, a pull on the network cable acts directly on the mating connector. **Both boards
must be screwed down**, positioned so the screws take the force. That mounting is needed anyway for the
thermal path, so the requirements agree rather than conflict.

**Both halves are Würth WR-PHD**, so contact heights match by design. An earlier draft of this entry
claimed Würth had no right-angle sockets and specified Sullins for that half; that was wrong. The range is
`6130`+contacts+rows+`43121`, and it exists.

**The socket range has gaps, which sets the connector sizes.** There is no 2×8 right-angle socket, so J2
and J3 use **2×10** with four spare contacts, and J1 uses **2×2** rather than 1×4. The pin header range has
every size; the sockets do not.

**The real requirement is mounting play.** Both boards pinned rigidly to exact positions would leave the
joint permanently stressed. One board's mounting holes get clearance, Ø3.4 mm for M3 or slots along the
mating direction, so the assembly settles as the screws are tightened.

**Connector pairs:** each board carries the socket on its upstream side (`613004243121` 2×2 for J1,
`613020243121` 2×10 for J2 and J3), the mating board the right-angle pin header (`61300421021`,
`61302021021`). The mainboard therefore has J1 and J2 only; the power board has J1, J2 and J3. The socket-on-mainboard rule is unchanged from D30's reasoning, and
coplanar adds a practical argument: a bare mainboard on the bench has no exposed pins for a probe to
short.

**Amended 2026-09-09.** The "socket upstream" rule above was wrong for `J2`: its 3.3 V is live whenever PoE
is present, so a header on the power board is a live header. The rule is now *the socket sits on the board
that sources what the connector carries*: `J1` socket on the mainboard, `J2` and `J3` sockets on the power
board, `J4` socket on the mainboard, and the IO board carries headers only. See the connector pairs in
`06`. D35 added `J4`, so the mainboard has three board-to-board connectors, not two.

**Still open:** whether the three boards sit in a row or in an L. That fixes which edge of the mainboard
carries J1 and J2 and therefore its outline. See Q10 in [11](11-open-questions.md).

---

## D32 — ESP32-S3 with a W5500, because the IO board interface must not break

**Decided.** The mainboard carries an **ESP32-S3-WROOM-1** and a **W5500** over SPI. The IO board link is
USB full-speed from rev A.

| | Part | $ | DigiKey stock |
|---|---|---|---|
| MCU | `ESP32-S3-WROOM-1-N4` | 5.21 | 2,176 |
| Ethernet | `W5500` | 3.47 | 73,368 |

**The requirement that forced it:** the IO board link has to be USB now, not later, so that IO boards work
unchanged on generation 2 hardware (D20). The IO boards are the growing half of the catalogue and the half
that ends up in the field; the mainboard is rebuilt anyway.

**Why this combination and not another:**

| | Eth MAC | USB host | Available |
|---|---|---|---|
| ESP32 classic | internal | **none** | yes |
| **ESP32-S3** | **no** | **OTG FS, 8 channels** | **yes** |
| ESP32-P4 | internal | OTG HS, 16 channels | **no, zero stock, no module** |
| ESP32-S31 | internal, 1 Gbps | OTG HS | listed, **zero stock** |

No shipping part has both. The P4 would have kept the internal MAC but is not purchasable and exists only
as a bare QFN80, and the S31 that solves everything is not in stock either. With USB non-negotiable, the S3
is the only option left, and the MAC goes.

**What it costs, stated plainly:** D3 falls. The W5500 runs in MACRAW mode at a practical 8 to 15 Mbit/s,
and an Art-Net console broadcasting 64 universes puts 13.4 Mbit/s on every node, which must be received and
discarded. That is at the ceiling, and the failure mode is dropped packets and stuttering fades rather than
a clean error.

**The mitigation was already in the design.** sACN with a correct IGMP join delivers only the universes a
node subscribed to, 0.2 Mbit/s regardless of rig size. Under D3 that was the preferred protocol; under D32
it is **required** in any installation large enough to matter, and Art-Net broadcast becomes a documented
limitation rather than a supported mode. See [08 — Firmware & Network](08-firmware-and-network.md).

**Three consequences worth knowing:**

The RMII section disappears, and with it the 50 MHz oscillator, the GPIO0 strapping problem, the oscillator
enable on GPIO16 and nine impedance-controlled RMII traces. The mainboard gets simpler, not more complex.
D5 and Q6 close, Q7 closes.

**USB OTG and USB Serial/JTAG share GPIO19/20 on the S3.** With OTG running as host, the S3 is flashed over
UART rather than USB. Route the console header accordingly.

**Wi-Fi and BLE come along unused.** They cost nothing, the module approval covers them, and BLE stays
available if a maintenance channel is ever wanted.

### Two things that came out of building it

**The Halo ring LEDs are addressable RGB, not single colour.** Two `IN-PI15TAT5R5G5B` (1.5 × 1.5 mm,
**0.65 mm high**, inside Neutrik's 0.80 mm limit) cascaded on one data line. That gives both ring halves
full colour for status — green running, red fault, blue for `/api/v1/identify`, amber during OTA — and
costs one GPIO instead of two.

**They need 3.7 to 5.5 V, so the data line needs a level shifter.** A 3.3 V GPIO does not reliably reach
the 0.7 × VDD input threshold of a 5 V WS2812-type LED. Use a **`74AHCT1G125W5-7`**, $0.17: the AHCT family
has TTL input thresholds at 5 V supply (VIH 2.0 V), which is exactly this problem. LVC and AHC parts have
CMOS thresholds at 3.5 V and do not work here, including the `74LVC1G17` already in Part-DB.

An `SN74LVC2T45` was considered first and works, but at $0.67 it is four times the price for a
bidirectional two-channel translator on a unidirectional single line.

**Rejected: classic ESP32 plus MAX3421E.** It would have kept the internal MAC and added a USB host over
SPI. At **$15.35** the host controller costs more than the MCU and PHY together, it is full-speed only, and
TinyUSB's MAX3421E host driver lives on Arduino and RP2040 rather than ESP-IDF. Software risk on top of
cost.

---

## D33 — ESP32-S31 is generation 2

**Decided as direction, not yet as a part.** The second-generation mainboard targets the **ESP32-S31**,
which resolves everything D32 had to compromise on.

| ESP32-S31 | |
|---|---|
| CPU | Dual-core RISC-V, 320 MHz |
| Wireless | Wi-Fi 6, BLE 5.4, Bluetooth Classic, 802.15.4 |
| Ethernet | **1000 Mbps MAC**, RMII |
| USB | **USB 2.0 High-Speed OTG, host and device**, plus Serial/JTAG |
| Memory | 544 kB SRAM, 16 or 32 MB in-package PSRAM |
| Temperature | −40 to +85 °C |
| Modules | `ESP32-S31-WROOM-1-N16R16V` $7.54, `-3-N16R16V` $7.30, 16 MB flash |

**It undoes D32's compromise entirely**: internal MAC returns, so D3's broadcast argument is satisfied
again; USB is high-speed with more host channels, so a backplane with a hub becomes possible; and the
radio returns for free.

**The blocker is procurement, not design.** Both modules show **zero DigiKey stock** despite being listed
as active and in mass production. Before committing a generation 2 mainboard, get real lead times from
Mouser, Farnell, LCSC or Espressif directly.

**The IO boards do not change**, which is the entire point of D20. A generation 1 IO board runs unmodified
on a generation 2 mainboard.

**Deliberately deferred to generation 2:** the backplane with a USB hub and multiple IO boards (which
reopens D21), and any wireless power board variant (D34 sketch below).

---

## D34 — Wireless belongs on the power board, not the mainboard

**Sketch, not yet decided.** If a battery or DC-powered node is ever built, the radio goes on that power
board rather than on the mainboard.

**Why it fits:** the power board already determines the node's power class. Letting it determine network
attachment as well is consistent. A PoE node has a cable and needs no radio; a battery node has no cable
and needs one. The mainboard stays identical either way.

**How it would attach:** an ESP32-C6 over `esp-hosted`, which carries Wi-Fi *and* Bluetooth to the host.
SDIO 4-bit gives 79.5 Mbit/s but needs 6 lines at 40 MHz across a 2.54 mm connector; SPI gives ~26 Mbit/s
over 5 lines and is the more robust choice, since the Wi-Fi PHY at ~36 Mbit/s is the bottleneck anyway.
UART at 3.3 Mbit/s is too slow.

**Two problems to solve first.** J2 has only four spare contacts and SPI needs five or six, so the
connector or the pinout would have to change. And **Art-Net broadcast over Wi-Fi does not work**: broadcast
and multicast frames are sent at the lowest basic rate, typically 1 to 6 Mbit/s, not at the nominal data
rate. A wireless node is fine for MQTT events and a few channels, and unusable as a DMX receiver in a
broadcast rig. That limitation belongs in the product documentation, not in a footnote.

---

## D35 — Mainboard and power board side by side, IO board across the back of both

**Decided.** The three boards stay coplanar (D31), but not in a row. Mainboard and power board sit side by
side; the IO board lies across the back edge of both and plugs into each of them directly.

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

**Why: it gets the link off the power board without stacking.** D31's row put the power board between the
other two, so `USB_D±`, `IO_nRESET`, `IO_BOOTSEL` and `IO_ATTN` had to cross a board carrying several
switching regulators and two more connectors. D31 itself flagged that as needing rework for generation 2's
high-speed USB. Here the mainboard reaches the IO board directly on `J4`, and the power board reaches it
directly on `J3`. Both of D31's original goals survive: the IO board's supply still never crosses the
mainboard, and both outward-facing boards still sit at an enclosure wall.

**A stacked power board was considered and rejected.** Bridging the power board over the mainboard and into
the IO board's area achieved the same routing, and cost more than it was worth:

- **Thermal.** The power board is the hot one, ~8 W at BT4, and it would sit directly over the ESP32 and the
  W5500. That is D31's headline argument thrown away.
- **Isolation becomes three-dimensional.** Domain 1 above Domain 2 copper turns a visible line into a
  volume, and the number to prove is creepage around a standoff rather than a gap you can see.
- **The transformer sets the stack height.** T1 is 8.8 mm. The paired WR-PHD socket and header give about
  **7.9 mm** — the socket body stands 5.4 mm above the board and the header's insulator adds 2.54 mm — so the
  standard parts do not fit and an elevated socket (Samtec ESQ, 11.05 mm) becomes a special part with a
  custom footprint.
- **The IO board gains a height keep-out** under the overhang, a new clause in an interface that must not
  change once boards ship.

**What it costs.**

**The connectors go at the middle of the shared edge.** `J4` at the mainboard's inner corner and `J3` at the
power board's inner corner, close together, so the IO board may be as narrow as it likes and docks in the
middle. Placed at the outer corners instead, every IO board would have to be as wide as the two boards
together — a lot of empty copper on a reed-switch board. **This is a contract item; fix it before layout.**

**The power board's width is fixed** by `J3` having to meet `J4`. Depth stays free. See D18.

**The ground topology becomes a triangle.** Mainboard → power → IO → mainboard is now a loop, where D31's
row was a chain. `J4`'s ground is the USB reference while `J3` carries the rail return, and an LED board's
current will divide between them. **`J3` must win on copper, not on intent**: wide, low-impedance return on
`J3`, signal-reference grounds on `J4`. A third-party IO board designer cannot infer this, so it belongs in
`06`.

**Consequences for the connectors:** `J3` loses everything but rails and ground and is sized for current
rather than pin count. `J2` loses the five signals it was passing through. **`J4` is new.** `J1` is
untouched.


---

## D36 — 5 V is the unconditional rail; every board makes its own 3.3 V

**Decided 2026-09-09.** The power board no longer generates 3.3 V. Its unconditional rail, the one that is
up whenever the PD module is up and that the boot sequence starts from, is **5 V**. Every board regulates
3.3 V for itself with an LDO: the mainboard with an AMS1117-3.3 from the 5 V on `J2`, an IO board from the
5 V on `J3`.

**Why.** Both 3.3 V consumers are small, the mainboard at about 350 mA and an IO board's RP2354 at about
100 mA, and for them every power board variant carried a buck, an eFuse, a descriptor rail and a metering
channel. On `PWR-BT4` that was a third LM61495 with its inductor and capacitors, about 250 mm². The rail
also arrived at the ESP32-S3 through two connectors and was shared with whatever the IO board did with it.
A regulator next to the processor is the better supply and the smaller power board at once.

**What changes.**

- `J2` pins 1 and 2 become `+5V`, unconditional, alongside pin 5. The mainboard sees one rail.
- `J3` pin 15 becomes a fifth `+5V` contact. There is no 3.3 V on `J3`.
- The power board's 5 V has two branches: through an eFuse behind `PWR_EN` to `J3`, and through an eFuse
  with its enable tied high to `J2`. The second eFuse is the part that guarded 3.3 V; it keeps current
  limit and inrush on the mainboard feed without firmware.
- The freed INA3221 channel meters the mainboard feed. Descriptor: `rails_present` bit 0 is `5V_MB` and
  always set, `limit_mA_3v3` and `shunt_uohm_3v3` become `limit_mA_5v_mb` and `shunt_uohm_5v_mb`. Offsets
  do not move.
- The IO board descriptor loses `power_mA_3v3`; a board declares its 3.3 V consumption inside `power_mA_5v`.
- The mainboard gets an **AMS1117-3.3**, not a buck. At about 250 mA with Wi-Fi off it dissipates 0.4 W,
  at 500 mA with Wi-Fi transmitting 0.85 W, which a SOT-223 on a copper pour carries at about 95 °C
  junction in a 50 °C enclosure. Wi-Fi is a fallback path (01), not a duty cycle. The AP63203 buck that
  made 3.3 V on `PWR-AF` was considered and rejected for the mainboard: it would be the only switcher on
  the board that carries every impedance-controlled trace, the PHY and the magnetics, and 0.4 W is 3 % of
  a `PWR-AF` budget and nothing on a `PWR-BT4`. The AMS1117 wants 22 µF with some ESR on its output;
  small ceramics alone can oscillate it.

**What it costs.** 0.3 to 0.6 W more heat on the mainboard than a buck would make, and an LDO on every IO
board, which the RP2354 base sheet carried anyway. The contract change is free because nothing has
shipped.

**Amends** D12 and D24, and the "only 3.3 V is guaranteed" statements in `02`, `04`, `05` and `06`, which
now read 5 V. Resolves Q21.

---

## D37 — V0 is one board: the mainboard plus an af module and the IO of the first haunt

**Decided 2026-09-09.** Before the three-board family is built, a single-PCB prototype proves the ESP32-S3,
the W5500, PoE and the firmware. It is `OH_Node_V0_Prototype`, started as a copy of the mainboard schematic.

**Why.** Five PCBs (mainboard, `PWR-BT4`, `IO-LED8`, and their successors) for a first prototype is too
expensive and proves too much at once. D16 through D36 stay the design for the product; V0 is the
shortest path to a node that boots, gets an address, fires a relay and lights a strip.

**What V0 carries.** One PoE class, no board-to-board connectors, no RP2354, no descriptor EEPROM, no
per-rail metering. The ESP32-S3 drives every output directly.

| Function | Part | Source | Notes |
|---|---|---|---|
| Ethernet + magnetics | Würth **7499511420** magjack | Part-DB 159, LRV symbol and footprint exist | 1000 mA per centre tap, Bob Smith and 1 nF/2 kV built in, two LEDs |
| PoE | Silvertel **AG9905-MTB**, 5 V | new, €7.49, 3121 stock | 9 W to 70 °C, 6 W at 85 °C, 1.8 A. Class 0. Min load 200 mA |
| Input bridges | 4 × **V6K100DUHM3/H** | Part-DB 157, as on `PWR-BT4` | datasheet suggests MB6S, the stocked dual diodes do the same |
| Input TVS | **SMAJ58A** | Part-DB 164 | Figure 14 of the Ag9900 datasheet |
| 3.3 V | **AMS1117-3.3** | Part-DB 12 | as D36 already said for the mainboard |
| Relays | 4 × Omron **G5LE-1 DC5**, SPDT 10 A | new, €1.60, 6304 stock | 79 mA coil, 1.6 W with all four held |
| Relay drivers | 2 × **DMN601DWK-7**, 4 × **1N4148WS** | Part-DB 28, 528 | |
| Relay terminals | 4 × WAGO **2601-1103**, 3-pos | new, €3.06 | NC / COM / NO. The 2-pos 2601-1102 is stocked, the 3-pos is not |
| PIR inputs | 2 × WR-WTB Interkom **66100311722**, 3-pin right angle | Part-DB 98 | 5 V, GND, signal. HC-SR501 style, 3.3 V output |
| WS2812 outputs | 2 × **74AHCT1G125W5-7** + 150 R | Part-DB 561 | data and GND only, strip power is external |
| WS2812 terminals | 2 × WR-WTB **61900211021**, 2-pin right angle | Part-DB 93 | |
| Audio | **MAX98357AETE+T** I2S class D | new, €1.90 | 3.2 W into 4 Ω from 5 V. Audio decoded by the ESP32-S3 from its 16 MB flash |
| Speaker | WAGO **2601-1102** and a CUI **SJ1-3523N** 3.5 mm jack in parallel | Part-DB 130, new €0.96 | the jack carries the bridged speaker output, not line level |
| Status LED | 1 × **IN-PI15TAT5R5G5B** on the existing 74AHCT1G125 | Part-DB | the second Halo LED goes with the etherCON |
| Spare GPIO | 1×10 header: 3V3, GND, SDA, SCL, 6 GPIO | Part-DB stock | |

**Power budget at 5 V.** ESP32-S3 and W5500 through the LDO 1.8 W typical and 2.5 W peak, four relays
1.6 W, amplifier 1 W typical and 3.5 W flat out, LEDs 0.2 W. About 4.5 W typical, 7.5 W worst case, against
9 W from the module and 6 W once the module passes 70 °C. Worst case at high ambient does not fit; V0
accepts that. The Ag9905 needs 200 mA minimum load or the PSE may drop it; the processor alone covers it,
so do not put the ESP32 into deep sleep on V0.

**Why 5 V and not 12 V.** The 12 V module keeps the stocked G2R relays and would allow 12 W, at the cost
of an AP3441 buck with inductor and capacitors on a board that has no switcher otherwise. Every V0 load
runs from 5 V, so the 5 V module removes a whole stage.

**Amended 2026-10-05: V0 moves to 12 V after all.** The sensors the haunt actually uses run from 12 V and
switch a contact to GND, so the PIR terminals need 12 V, and D11's argument applies to V0 too: boosting
5 V to 12 V would have needed a boost stage that cannot limit a short on the sensor cable, on top of a 5 V
budget that was already 7.5 W worst case against 9 W. V0 now carries the product's own power path from D12
and D29: **AG9912-MTB** (12 V, 12 W to 70 °C, min load 100 mA, same Ag9900 datasheet and MTB footprint) and an
**AP63205WU-7** buck to 5 V. The original reasoning above stays true; the sensor requirement outweighed it.
**Correction:** the AP3441 named above is not usable for 12 V → 5 V; its input range is 2.7–5.5 V. Details
and the new budget in docs/12.

**Why the magjack and not the etherCON with discrete magnetics.** D26 and D28 stand for the product.
For a prototype the NE8FAH-LR-DAE, the 7490220122 transformer, the Halo LEDs and the external Bob Smith
network cost more than the magjack and take a board edge the prototype does not need. The magjack is
1000 mA per tap, which is Type 3 territory, so nothing about the PoE front end is being under-built.

**Why an I2S amplifier and not a DFPlayer.** The DFPlayer would have been the faster path. The
MAX98357A is one QFN, three GPIOs and no module socket, and it forces the sound-storage question of Q20
onto the ESP32-S3, which has 16 MB of flash it is not using. Q20 stays open for the product.

**What V0 does not prove.** The IO board link, the RP2354 firmware, the descriptor scheme, per-rail
eFuses and metering, the Domain 1 crossing on `J1`, and anything above 9 W. Those are what the three
boards are for.

**Isolation on one board.** Domain 1 is now the magjack centre taps `V1..V4`, the bridges, the TVS and
the Ag9905 input pins. The 1500 V barrier is the module's transformer and the magjack's. Keep the
Domain 1 keep-out around that corner exactly as D17 required for `J1`.

**Does not supersede** D16, D17, D19, D20, D26, D28, D29 or D35. V0 is a prototype, not a variant.
