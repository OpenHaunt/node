# OH_Node_V0_Prototype — V0.2

The first V0 was ordered at JLCPCB on 2026-10-05 from commit `756af21`, tagged `v0.1` (its order files are in `production/jlc_final/` at that tag; the folder now holds V0.2).
The pre-fab list, its schematic review and both layout reviews are in the git history of this file. This
list covers the second revision of the same board, from bring-up findings and later feedback.

## State (2026-10-17)

Schematic and layout are done and routed. DRC is clean on the board saved 18:17 (refilled zones, schematic
parity): no errors, 0 unconnected, only 4 × `silk_edge_clearance` from J1's frame on the top edge. SW1/SW2
3D model rotation fixed in the footprint (`-90 0 90`) and updated on the board. ERC: only the old U6
PAD warning. Every symbol except the test points and mounting holes comes from Part-DB.

## Open before ordering

- [ ] **Optional: +VDC plane at PS1 pad 1** (datasheet Fig. 12). VIN− and −VDC planes are in; VPOE leaves pad 1 as
      a 1.0 mm track only. A VPOE zone on F.Cu/B.Cu at about x 74.1–78.4, y 108.9–114.7 with a few vias.
- [ ] Board marking: name, "Node v0.2" and "Designed by hrueger" are on; date and project URL/license are not.
- [x] JLC files regenerated in `production/jlc_final/` (2026-10-17 20:44, board saved 20:42): 154 SMD parts, 54
      lines, hand-parts lists.
- [ ] Check the new parts in JLC's placement preview (list in `production/jlc_final/README.md`), set
      `FT Rotation Offset` where one is off and regenerate. Add LCSC C23630 to Part-DB for CL10A225KO8NNNC.
- [x] Docs: docs/12 "V0.2 board changes" (PIR stage, LEDs, audio, buttons, PS1 thermal, STX-3000 pinout,
      stitching) and the V0.2 pin plan.
- [x] kicad-custom-libs committed and pushed (`fabc0bb`..`c865e47`): TPS22919 QOD fix, TPS2121RUX + VQFN-HR-12,
      STX-3000, Ag9900 thermal vias, TS-1187A 3D rotation, CJ431.
- [x] **U8 to XREF mode (fast switchover, datasheet 10.4).** CJ431 (C3113, JLC basic, `LRV:CJ431`: pin 1 REF,
      2 K, 3 A, *not* the TL431DBZ order) as 2.5 V shunt reference: 1K from +5V to K, REF tied to K, A to GND,
      K to U8 CP2 (pin 3, now GND). PR1 divider R66 33K → 10K, R68 10K → 12K (0402WGF1202TCE, C25752, JLC
      basic): PoE preferred above 2.5 V × 22/12 = 4.57 V (±1 % reference, 1 % resistors: about 4.45–4.69 V),
      switchover in 5 µs instead of 100 µs. Without +5V, CP2 is low and the internal reference picks PoE as before.
      _Done 2026-10-17 (net `PWRMUX_REF`, Part-DB 631/632), together with L3 BLM15AG601SN1D for the DAC's
      AVDD/CPVDD (C40, C41, C43, C49 on `/AVDD`, PWR_FLAG). U10 sits 2.7 mm from CP2. `LRV:CJ431` in kicad-custom-libs
      `c865e47`._
- [ ] Tag `v0.2` on the commit that is ordered.

## Done in V0.2

- [x] **5 V PoE module, boost to 12 V, runs from a power bank** (decided 2026-10-17, docs/12 "V0.2 power
      path"). PS1 AG9905-MTB, U8 TPS2121 power mux (PoE on IN1 preferred, USB on IN2, 2.4 A limit, soft
      start), U3 straight on +5V, MT3608 (U7) boost to +12V for F1/F2 and J12. The AP63205 buck, the
      TPS22919, D10, D13 and D20 are gone. Switch node 1.0–1.5 mm, VBUS 1.0 mm (12 mm at 0.6 mm).
- [x] **Limit USB inrush:** U8's soft start (C38 100 n, ~0.14 A into ~180 µF). Still confirm on the hub that broke
      V0.1 (bring-up).
- [x] **Back-feed with USB only:** U8 blocks reverse current, and VBUS only reaches J11, C39 and U8 IN2.
- [x] **Reset button:** SW2 on EN (silk RST), BOOT is SW1 on IO0.
- [x] **Line out with PCM5102A, MAX98357A stays.** Both share IO40/41/42; XSMT on GPIO39 with 10K pull-down,
      AMP_EN on IO48, output RC 470 Ω / 2.2 nF per channel. MAX98357A on J13 (691137710002). Firmware calls
      `audioBegin(..., false)` for stereo.
- [x] **J1 (STX-3000) tip/ring fixed in the footprint** (2026-10-17). Kycon pin 1 = sleeve, pin 2 = middle
      spring (ring), pin 3 = end contact (tip), the same symbol as CUI's SJ1-3523N and Kycon's STX-3501-3N
      (where the end-contact pin sits at the back of the jack). The LRV footprint had 2 = "T", 3 = "R"; now
      swapped, J1 updated and AUD_L/AUD_R re-routed. Pins 2 and 3 sit at the same depth, so the symbol is the
      only evidence. Confirmed by ear on a built V0.2 (2026-10-26 tones, 2026-10-27 spoken left/right), not
      with a meter; see docs/12 "STX-3000 pinout".
- [x] **Speaker plays (L+R)/2:** SD_MODE through R69 47K from GPIO48 with R70 10K to GND, ~0.53 V (window
      0.355–0.65 V, datasheet Table 5). GPIO48 low or floating = shutdown. Volume cannot be set per output:
      neither chip has a volume control, both get the same stream; XSMT and AMP_EN mute each output.
- [x] **PIR1/PIR2 swapped in the schematic** to match the board: J8 → Q3 → `/PIR2`, J10 → Q4 → `/PIR1`. The LED
      labels followed when the LEDs moved: D16 next to J8 is `LED_PIR2` (GPIO45), D17 next to J10 `LED_PIR1`
      (GPIO46).
- [x] **Motion sensor inputs (J8/J10), 12 V tolerant, no back-feed into 3V3:** MMBT3904 stage per input,
      terminal pull-up through a 1N4148WS, 10K to the base, 100K base–GND, collector to the GPIO with 10K to
      3V3. Logic is inverted (firmware). +12V to the terminals through F1/F2.
- [x] **PS1 footprint** checked against the Ag9900 datasheet V3.5: pads, pitch and the isolation keep-out match
      Fig. 22 (hand soldering) and Fig. 21 (reflow) to 0.01 mm, the pin order matches Table 2.
- [x] **PS1 thermal relief (Fig. 12):** 25 VIN− vias with a silk frame added to both `LRV:AG9900-MTB*`
      footprints; VIN− zone on all four layers inside the isolation ring; −VDC is the GND pour with vias at x 78.
- [x] **Status LEDs:** relays D3/D4/D14/D15 (KT-0603R), PIRs D16/D17 (KT-0805Y), each on its own GPIO, low-side.
      Relay LEDs at the same offset from each relay.
- [x] **GND stitching:** 162 GND vias added (pads near far GND pads, return vias at signal vias, general
      stitching). Maps in `OH_Node_V0_Prototype/analysis/2026-10-17_gnd/`. Left: U1 pins 9/14/16, R8, C33 are
      1.1–1.4 mm from a via; LED_RLY4's via has no return via (slow signal).
- [x] Silkscreen: COM/NO/NC on the relay terminals, labels on J7–J10 and J12, BOOT/RST, STAT/ERR, test points.

## Bring-up checks

- [ ] ~~Input ripple at U7 (AP63205)~~: the buck is gone in V0.2. Still worth probing on the ordered V0.1.
- [ ] V0.2: +12V from the MT3608 at no load and with both sensors; C36/C37 are 22 µ/25 V 0805 at 12 V bias,
      an estimated 10 µF effective together.
- [ ] W5500 SPI: SCLK, MOSI, MISO, SCSn are on GPIO9 to 12 and run through the GPIO matrix (the direct FSPI pins
      are GPIO10 to 13 in a different order). Start at 20 MHz. For the next revision, consider the direct pins.
- [ ] The checks already listed in docs/12: which source carries the load with PoE and USB together; +12V and
      the current into PS1 +VDC with USB only (V0.1). For V0.2: +5V on PoE only, USB only and both; the +5V
      dip when PoE is unplugged with USB connected; USB inrush on the hub that broke V0.1.
- [ ] J1: tip = left, ring = right with a stereo test signal.
