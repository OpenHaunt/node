# Enclosures

3D-printed enclosures for the V0 prototype, one folder per board revision:

| | Board | Plate |
|---|---|---|
| `v0.1/` | tag `v0.1` | `v0.1/OH_Node_V0.1_enclosure.3mf` |
| `v0.2/` | tag `v0.2`, the board in this folder | `v0.2/OH_Node_V0.2_enclosure.3mf` |

Shared by both:

| File | |
|---|---|
| `make_3mf.py` | lays the rendered parts out as a PrusaSlicer plate, one named object per part, brim on the pins |
| `check_print.py` | printability: each part one closed solid, on the bed, inside the Prusa XL volume |
| `check_collision.py` | parts against the board's 3D models |

Each folder's `build.sh` writes its 3MF and runs the print check; its README has the parts list
(screws, inserts, filament) and the assembly steps.
