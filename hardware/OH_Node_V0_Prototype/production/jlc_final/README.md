# OH_Node_V0_Prototype — JLCPCB order files (2026-10-01)

| File | Upload as | Source |
|---|---|---|
| `OH_Node_V0_Prototype_gerbers.zip` | Gerber file | Fabrication Toolkit plugin, run 2026-10-01 18:49 on the final board |
| `bom.csv` | BOM | `analysis/make_bom.py` from the netlist and Part-DB LCSC order details |
| `positions.csv` | CPL / pick-and-place | plugin output, reduced to the 88 SMD parts in the BOM |

## PCB options

- 4 layers, 120 × 90 mm, 1.6 mm, 1 oz outer copper. Any JLC 4-layer stackup is fine: the only
  controlled-impedance nets (Ethernet, USB) are too short to matter at 100 Mbit / USB full speed, so
  **no impedance control** needs to be ordered.
- Surface finish free choice (ENIG was the AISLER default; HASL is fine for this board).
- Mains on the board: 230 V on the relay contacts, J2/J4/J5/J6. No special PCB option needed.

## Assembly

- Top side only, 88 SMD parts, 36 BOM lines. 
- Extended parts (loading fee each): C408132 (10 µ 0402), C11692 (12k4), USB4215, BLM15, DMN601DWK, W5500,
  ESP32-S3-WROOM-1-N16R2, MAX98357A, ECS crystal, 16SVPC100M. The rest are basic or preferred.
- **Check in JLC's placement preview**, polarity and pin 1 of: C2 (polymer cap), D9 (TVS), D10, D11/D12
  (bridges, + on pin 1), U1, U3, U4/U5, U6, J11, Y1. The plugin rotated C2, Q1/Q2, U1, U3 and U4/U5 to
  JLC's conventions; the preview is the place to confirm it.

## Not assembled by JLC — hand-soldered

| Ref | Part | Source |
|---|---|---|
| PS1 | Silvertel AG9905-MTB PoE module | DigiKey 5061-AG9905-MTB-ND |
| K1–K4 | Omron G2RL-1-E DC5 relays | DigiKey Z2824-ND |
| J2, J4, J5, J6, J8, J10 | Würth 691137710003 3-pole screw terminals | DigiKey 732-10956-ND |
| J7, J9 | Würth 691137710002 2-pole screw terminals | DigiKey 732-10955-ND |
| J3 | Würth 7499511420 magjack | Part-DB 159 |
| J1 | CUI SJ1-3523N audio jack | Part-DB 581 |
| J12 | Würth 61300811121 1×8 header | Part-DB 34 |
| H1–H4 | M2.5 mounting holes | — |

DNP, not in BOM or CPL: R5 (chassis 0 R bond option), R9–R14 (W5500 PMODE straps).

## Checks on the released files

- DRC: 0 errors, 0 unconnected, schematic parity clean; only silkscreen warnings remain.
- ERC: only the known MAX98357A exposed-pad pin-type warning.
- Mains copper ≥ 6.4 mm from everything else on every layer; PoE barrier ≥ 3 mm.
- Gerber set complete (4 copper, mask, paste, silk, edge, PTH + NPTH drills), outline 120 × 90 mm.
- CPL designators == BOM designators (88).
