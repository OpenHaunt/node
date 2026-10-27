# Enclosures

3D-printed enclosures for the V0 prototype, one folder per board revision:

| | Board | Plate |
|---|---|---|
| `v0.1/` | tag `v0.1` | `v0.1/OH_Node_V0.1_enclosure.3mf` |
| `v0.2/` | tag `v0.2`, the board in this folder | `v0.2/OH_Node_V0.2_enclosure.3mf` |
| both | 2 × v0.2 and 1 × v0.1 on one plate, written by `build_plate.sh` | `OH_Node_V0_2x0.2_1x0.1_enclosures.3mf` |
| `v0.2/` × 3 | 3 × v0.2 on one plate, written by `build_plate_3x0.2.sh` | `OH_Node_V0_3x0.2_enclosures.3mf` |

Shared by both:

| File | |
|---|---|
| `make_3mf.py` | lays the rendered parts out as a PrusaSlicer plate, one named object per part, brim on the pins |
| `check_print.py` | printability: each part one closed solid, on the bed, inside the Prusa XL volume |
| `check_collision.py` | parts against the board's 3D models |
| `wiring_terminals.svg` | which screw of the relay, PIR and WS2812 terminals is what, seen on the lid; numbered as the firmware does |

Each folder's `build.sh` writes its 3MF and runs the print check; its README has the parts list
(screws, inserts, filament) and the assembly steps.
