# OH_Node_V0_Prototype — JLCPCB order files, V0.2 (2026-10-17)

Second revision of the V0 board. Replaces the V0.1 set (ordered 2026-10-05); that one is in git at tag `v0.1`.
What changed is in `hardware/ToDo.md` ("Done in V0.2") and docs/12 ("V0.2 power path", "V0.2 board changes"):
5 V PoE module with a TPS2121 power mux (fast switchover on a CJ431 reference) and an MT3608 12 V boost, PCM5102A line out on a Kycon STX-3000,
ferrite-filtered DAC supply, MAX98357A on a speaker terminal, 12 V tolerant PIR inputs, six status LEDs, reset button, PS1 thermal vias,
GND stitching.

| File | Upload as | Source |
|---|---|---|
| `OH_Node_V0_Prototype_gerbers.zip` | Gerber file | Fabrication Toolkit plugin CLI (`-t -f -e`, options as in `fabrication-toolkit-options.json`), run 2026-10-17 23:48 on the board saved 23:47 |
| `bom.csv` | BOM | `scripts/make_bom.py` from the netlist and Part-DB LCSC order details; C45/C48 (CL10A225KO8NNNC) set to C23630 by hand, Part-DB has no LCSC order detail for it yet |
| `positions.csv` | CPL / pick-and-place | plugin output, reduced to the 154 SMD parts in the BOM |
| `hand_parts.*` | — | `scripts/hand_parts.py --boards 5 --spares` |

`make_bom.py` was fixed for this set: it read each footprint's `(attr ...)` with a forward regex, so a
footprint without one (the test points) took the next footprint's attribute and that part fell out of the
BOM (D16, D17, R41, R60, U4 and Y1 here). The V0.1 BOM was checked against the V0.1 board and was not
affected.

## PCB options

As V0.1: 4 layers, 120 × 90 mm, 1.6 mm, 1 oz outer copper, JLC04161H-7628 if impedance control is ordered,
HASL is fine. Mains on the relay contacts J2/J4/J5/J6.

## Assembly

- Top side only, **154 SMD parts, 54 BOM lines**; every line has an LCSC number.
- **19 extended lines** (loading fee each), from JLC's parts search on 2026-10-17: C2 FP-025ME101M-SAR,
  C5/C27/C33/C43/C49/C50 GRM155R61A106ME11D, C21 GRM31CR61A107MEA8L, D1 KT-0603Y, F1/F2 0805L020/30XR, J11
  USB4215-03-A, L1 BLM15AG601SN1D, L2 SWPA4030S100MT, Q1/Q2 DMN2004DWK-7, R8 12k4, U1 W5500, U2
  ESP32-S3-WROOM-1-N16R2, U3 AMS1117-3.3, U4/U5 74AHCT1G125W5-7, U6 MAX98357A, U7 MT3608, U8 TPS2121RUXR, U9
  PCM5102APWR, Y1 ECS-250-18-33-JGN-TR3. All others basic or preferred. Confirm in JLC's BOM tool.
- Thin stock on 2026-10-17: Y1 (401), C2 (699), U2 (896), C21 (1073), J11 (1311). C2 fallback as before:
  RVT1E101M0607 (C72477), same CP_Elec_6.3x7.7 footprint.
- Unchanged from V0.1 and still set in the schematic and board: `FT Origin = Center` on U2 and J11,
  `FT Position Offset = 0,0.12` on U2, `FT Rotation Offset` +90° on Q1/Q2 and U4/U5, −90° on U6.
- New after the V0.2 order, from JLC's preview: `FT Rotation Offset` +90° on U7 MT3608 (CPL 90° → 180°),
  −90° on U8 TPS2121 (CPL 180° → 90°). Set in the schematic and board; the files here (tag `v0.2`) predate it,
  and for that order both were rotated in JLC's placement editor. The next regeneration picks it up.
- **Check in JLC's placement preview** (pin 1 dot or polarity):
  - Carried over: C2, D1/D2, D9, D11/D12, Q1/Q2, U1, U2, U3, U4/U5, U6, J11, Y1.
  - **New, no rotation offset yet:** U9 PCM5102A (TSSOP-20; the plugin wrote 270° for a 0° footprint, pin 1
    upper left), Q3/Q4 MMBT3904, D3/D4/D14/D15 and D16/D17 LEDs, D18/D19
    1N4148WS, D21 B5819W, L2, U10 CJ431 (SOT-23, pin 1 REF, 2 K, 3 A; the CPL says 180°). SW1/SW2 are
    symmetric.
  - If a part is off, set `FT Rotation Offset` on it (schematic and board) and regenerate, as for V0.1.
- Diodes: JLC marks anode "+" and cathode "−". Pad 1 is the cathode on every diode and LED here.
- **C2 has no 3D model at JLC.** Pin 1 (+, `VPOE`) is now the **lower** pad (y 109.5), pin 2 (GND) the upper
  one; the CPL says 270°. Order with "Confirm Parts Placement" and add the remark
  "C2: polarized, + (pin 1) at the '+' silkscreen mark (lower pad)".

## Not assembled by JLC — hand-soldered

| Ref | Part | Source |
|---|---|---|
| PS1 | Silvertel **AG9905-MTB** PoE module, 5 V | DigiKey 5061-AG9905-MTB-ND |
| K1–K4 | Omron G2RL-1-E DC5 relays | DigiKey Z2824-ND |
| J1 | Kycon STX-3000 audio jack | DigiKey 2092-STX-3000-ND |
| J2, J4, J5, J6, J8, J10 | Würth 691137710003 3-pole screw terminals | Würth direct |
| J7, J9, J13 | Würth 691137710002 2-pole screw terminals | Würth direct |
| J3 | Würth 7499511420 magjack | Würth direct |
| J12 | Würth 61300811121 1×8 header | Würth direct |
| H1–H4 | M2.5 mounting holes | — |

Quantities and prices for 5 boards plus spares in `hand_parts.md` (DigiKey €121.50, Würth reference
€106.77 at DigiKey prices).

DNP, not in BOM or CPL: R5, R9–R14, R47, R48, R50. Test points TP1–TP8 are bare pads.

## Checks on the released files

- DRC on the board the files were generated from (zones refilled): 0 errors, 0 unconnected, schematic
  parity clean; 4 warnings, J1's silkscreen frame at the top edge.
- ERC: only the MAX98357A thermal pad warning (U6 PAD, unspecified pin on GND).
- Gerber set complete: 4 copper layers, mask, paste, silk, edge, PTH + NPTH drills and drill maps.
- BOM designators == CPL designators (154), all on the top layer.
- Not yet: J1 tip/ring with a plug and a meter on a real STX-3000 (the footprint fix rests on Kycon's
  drawing symbol).
