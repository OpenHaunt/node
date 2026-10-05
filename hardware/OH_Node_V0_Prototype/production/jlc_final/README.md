# OH_Node_V0_Prototype — JLCPCB order files (2026-10-05, evening)

Replaces the 2026-10-01 set. Since then: the external layout review (ToDo.md), the move to 12 V (AG9912-MTB +
AP63205 buck, 12 V sensor terminals), GND stitching and the silkscreen cleanup.

Evening update of the same day, second layout review and schematic review (ToDo.md): R20/R49 10K pull-downs on
the WS2812 buffer inputs (new in BOM and CPL), C3 moved next to PS1, L2 rotated, J12 pin 2 now 3V3, PS1.2 via
moved out of the pad, GND via arrays at PS1.2 and the screw terminals, more return vias. No part changes
otherwise; hand-soldered parts unchanged.

| File | Upload as | Source |
|---|---|---|
| `OH_Node_V0_Prototype_gerbers.zip` | Gerber file | Fabrication Toolkit plugin (CLI, options from `fabrication-toolkit-options.json`), run 2026-10-05 evening on the board committed with these files |
| `bom.csv` | BOM | `scripts/make_bom.py` from the netlist and Part-DB LCSC order details |
| `positions.csv` | CPL / pick-and-place | plugin output, reduced to the 104 SMD parts in the BOM |
| `hand_parts.*` | — | `scripts/hand_parts.py --boards 5 --spares`: hand-soldered parts with spares (1 from €5 unit price, 2 from €1, 3 below), split into a DigiKey cart upload and a Würth direct order list |

## PCB options

- 4 layers, 120 × 90 mm, 1.6 mm, 1 oz outer copper.
- The USB (90 Ω) and Ethernet (100 Ω) pairs are routed to JLC's own calculator for **JLC04161H-7628**
  (0.247 / 0.15 and 0.183 / 0.15 mm). Pick that stackup if impedance control is ordered; at 100 Mbit and USB
  full speed over a few centimetres it is not required.
- Surface finish free choice (HASL is fine).
- Mains on the board: 230 V on the relay contacts, J2/J4/J5/J6. No special PCB option needed.

## Assembly

- Top side only, **104 SMD parts, 41 BOM lines**; every line has an LCSC number.
- 20 lines are basic/preferred. **21 extended** (loading fee each), as JLC's BOM tool classified them on
  2026-10-05: C2 FP-025ME101M-SAR, C5/C27/C33 GRM155R61A106ME11D, C17/C18 27p 0402CG270J500NT, C21
  GRM31CR61A107MEA8L, C22/C23 6n8 0402B682K500NT, D1 KT-0603Y, D9 SMAJ58A, F1/F2 0805L020/30XR, J11
  USB4215-03-A, L1 BLM15, L2 SWPA6045S4R7MT, Q1/Q2 DMN2004DWK-7, R8 12k4, R26/R27/R28/R33 150R, U1 W5500,
  U2 ESP32-S3-WROOM-1-N16R2, U3 AMS1117-3.3, U4/U5 74AHCT1G125W5-7, U6 MAX98357A, U7 AP63205WU-7, Y1
  ECS-250-18-33-JGN-TR3. JLC's matcher picked the same LCSC number as `bom.csv` on all 41 lines.
- Thin stock on 2026-10-05: U4/U5 (323), Y1 (477), J11 (1285), C2 (1800). C2 has a drop-in fallback:
  RVT1E101M0607 (C72477), same CP_Elec_6.3x7.7 footprint.
- **Check in JLC's placement preview**, polarity and pin 1 of: C2 (polymer cap), D1/D2 (LEDs), D9 (TVS), D10,
  D11/D12 (bridges, + on pin 1), Q1/Q2, U1, U3, U4/U5, U6, **U7**, J11, Y1. The plugin rotated C2, Q1/Q2, U1,
  U3, U4/U5 and U7 to JLC's conventions (`transformations.csv`); the preview is where that gets confirmed.
- **U2 and J11 carry `FT Origin = Center`** (schematic and board field), so the plugin writes the centre of the
  pads instead of the KiCad anchor. Without it, JLC's preview put the ESP32 module 3.6 mm towards the antenna
  (antenna past the board edge) and the USB-C pins short of their pads. CPL: U2 y 83.0 → 86.65 (with
  `FT Position Offset = 0,0.12`, matched to the preview), J11 y 73.25 → 74.125.
- **Rotation fixes from JLC's preview** (`FT Rotation Offset`, schematic and board): Q1/Q2, U4/U5 and U7 +90°
  (counter-clockwise), U6 −90°. The plugin's `transformations.csv` entries left them 90° off for these JLC
  models. Pad 1 on the board: Q1/Q2 lower right, U4/U5 and U7 upper right, U6 right side towards the bottom;
  the pink pin-1 dot in the preview must sit there.
- Diodes: JLC marks anode "+" and cathode "−". Pad 1 is the cathode on every diode here, so "−" sits in the
  closed end of the silkscreen bracket. D11/D12: "+" at pad 1 (the pin with the triangle marker, `/VIN+`).
- Y1 has no 3D model at JLC either. Pads 1/3 (crystal) and 2/4 (GND) are interchangeable by the ECS
  datasheet, so 0° and 180° both work; 90° does not fit the 1.7 × 2.2 mm pad pattern.
- **C2 has no 3D model at JLC** (C50146718), so the preview cannot show its polarity. Pin 1 (+, +12V) is the
  upper pad (y 109.8, the end with the "+" on the silkscreen), pin 2 (GND) the lower one; the CPL says 90°
  after the plugin's `CP_Elec_` +180° rule. Order with "Confirm Parts Placement" and add the remark
  "C2: polarized, + (pin 1) at the '+' silkscreen mark (upper pad)".

## Not assembled by JLC — hand-soldered

Würth Elektronik parts are ordered from Würth directly (`hand_parts_wurth.csv`, order code = MPN), the rest from
DigiKey (`hand_parts_digikey.csv`, cart upload).

| Ref | Part | Source |
|---|---|---|
| PS1 | Silvertel **AG9912-MTB** PoE module, 12 V | DigiKey 5061-AG9912-MTB-ND |
| K1–K4 | Omron G2RL-1-E DC5 relays | DigiKey Z2824-ND |
| J1 | CUI SJ1-3523N audio jack | DigiKey CP1-3523N-ND |
| J2, J4, J5, J6, J8, J10 | Würth 691137710003 3-pole screw terminals | Würth direct |
| J7, J9 | Würth 691137710002 2-pole screw terminals | Würth direct |
| J3 | Würth 7499511420 magjack | Würth direct |
| J12 | Würth 61300811121 1×8 header | Würth direct |
| H1–H4 | M2.5 mounting holes | — |

Prices and quantities for 5 boards plus spares in `hand_parts.md`.

DNP, not in BOM or CPL: R5 (chassis 0 R bond option), R9–R14 (W5500 PMODE straps), R47/R48/R50 (0 R from
mounting holes H1/H2/H4 to GND, fit for a metal enclosure). Test points TP1–TP8 are bare pads, not placed.

## Checks on the released files

- DRC: 0 errors, 0 warnings (six silkscreen-at-edge hits excluded), 0 unconnected, schematic parity clean.
- ERC: 0.
- Mains copper ≥ 6.4 mm from everything else on every layer (`mains_to_selv`); PoE barrier rule
  `isolation_barrier` (1.5 mm) clean.
- Gerber set complete: 4 copper layers, mask, paste, silk, edge, PTH + NPTH drills and drill maps.
- BOM designators == CPL designators (104), all on the top layer.
- Enclosure collision check against the current board: only the known J1 false positive.
