# V0.1 enclosure

OpenSCAD enclosure for the V0.1 prototype board (tag `v0.1`; the V0.2 board has its own in
`../v0.2`): base, hood and a push pin for SW1. Wall-mount tabs,
hood screwed down with 4 × M2.5 through the board's mounting holes.

| File | |
|---|---|
| `enclosure.scad` | the model; `part` selects what is rendered. The assembly view also shows screws and inserts (`show_hardware`); `part="screws_placed"` exports the screws for the collision check |
| `OH_Node_V0.1_enclosure.3mf` | PrusaSlicer plate: base, hood and the pin as separate named objects, already oriented, laid out for the Prusa XL |
| `build.sh` | renders the parts, writes the 3MF and runs the print check |
| `export_pcb.sh` | exports the populated V0.1 board from the `v0.1` tag as `pcb.stl` (58 MB, gitignored) for the assembly view |
| `../check_collision.py` | tests the parts against the board's 3D models |
| `../check_print.py` | tests each part of an STL or 3MF for printability: one closed solid, lying on the bed, fits a Prusa XL |
| `../make_3mf.py` | lays the rendered parts out as one 3MF plate, one object per part |

## Parts

Per enclosure. Part-DB status as of 2026-10-18. Inserts and screws both from 3DJake (3djake.de, ships from Austria).

| Qty | Part | Notes | Part-DB |
|---|---|---|---|
| 4 | Heat-set insert CNC Kitchen M2.5 Standard, M2.5 × 4.0 ([3DJake](https://www.3djake.de/cnc-kitchen/gewindeeinsatz-m25-standard), 100 pcs) | hole Ø4.0, blind, 6.7 deep with 1.0 closed floor below; the screw tip ends 1.2 above the bottom; min. wall 1.6 (boss Ø7.5). A ruthex RX-M2.5x5.7 fits the same hole | not yet, to order |
| 4 | Low cheese head screw ISO 14580 M2.5 × 25 A2-70, Torx T8 (CNC Kitchen, 25 pcs, [3DJake](https://www.3djake.de/cnc-kitchen/m25-zylinderschrauben-niedriger-kopf-iso-14580-edelstahl-a2-70), variant M2,5x25; ISO 14583 pan head fits too) | from the hood top through post and board into the insert; head Ø4.5 × 1.85 in a Ø5.4 seat 2.0 deep, so it sits 0.15 below the lid surface. Length 23.7–25.9 works: the 25 runs through the whole insert and ends 1.2 above the bore bottom | not yet, to order; ID 354 is the same screw at 12 mm (Bossard) |
| 4 | Wall screw: 4 mm wood screw with wall plug, or M4; pan or flat head Ø8–9 | through the 4.5 × 10.5 slots of the 4 mm thick tabs (16 × 20, gussets on both edges); not countersunk. Optional washer DIN 125 4.3 | washer: ID 286, only 2 in stock |
| 1 | Push pin | printed, in `OH_Node_V0.1_enclosure.3mf` | — |
| ~85 g | PETG, light or translucent | solid volume of all parts about 67 cm³; the LED skins need a light color. In stock: Prusament PETG Signal White (ID 304), Clear (ID 294) | in stock |

Tools: soldering iron with an insert tip (inserts at 10–20 °C above the print temperature), Torx T8.

Set `use_inserts = false` to tap M2.5 directly into the plastic instead (Ø2.2 bore).

## Printing

Prusa XL: open `OH_Node_V0.1_enclosure.3mf` in PrusaSlicer, the parts are already oriented and on the plate (any printer with a 150 × 95 mm bed works part by part). Base floor down, hood lid down (the LED skin then is the
first layer), pin top end down. 0.2 mm layers, no supports.
The 3MF gives the pins a 4 mm brim (they stand 18.65 mm tall on a Ø4 end); their collar has a 45° cone
on the bed side instead of a flat overhang. Sliced in PrusaSlicer 2.9.6 with 0.4 nozzle, 2 perimeters,
15 % infill, PETG: no stability warnings, about 72 g and 7 h 41 min.

The first version of the base had a 2.4 mm³ sliver floating next to the right-hand tab at y −72:
the terminal cut-out had removed the wall there, and the gusset sat inside the tab's screw slot. Since
2026-10-18 the base keeps its wall below the parting plane under all cut-outs, and the gussets sit on
the tab flanks. `check_print.py` fails a part with more than one body.
The LED windows over D1/D2 are a 0.4 mm skin and only let light through in a light-colored or
translucent filament. Set `led_skin = 0` for open holes.

The lid is 2.5 mm thick (2.0 until 2026-10-18) so the screw heads can sit flush; the hood is 0.5 mm taller, nothing inside moved.
The board sits 5.7 mm above the floor (4.5 until 2026-10-18), so the 6.7 mm insert bore stays closed at the bottom;
only the base walls grew, by 1.2 mm. Outer height 29.2 mm without tabs.

## Assembly

1. Press the inserts into the base, flush with the boss top (the bore is deeper than the insert, room for the screw tip).
2. Put the board on the bosses.
3. Drop the pin into the hood's guide from the inside, collar towards the board.
4. Lower the hood: all connector cut-outs are open at the bottom edge, so it slides over the jack barrel.
5. Screw down. The same screws hold the board, so with the hood off the board is loose.

## Rebuilding and checking

```sh
./build.sh                # → OH_Node_V0.1_enclosure.3mf, with the print check
./export_pcb.sh           # board for the assembly view and the collision check
# needs numpy, scipy, networkx, lxml, trimesh, manifold3d
for p in base hood pin; do openscad --backend Manifold -D "part=\"${p}_placed\"" -o /tmp/${p}_placed.stl enclosure.scad; done
python3 ../check_collision.py /tmp pcb.stl
```

The check intersects the convex hull of every 3D-model shell with each part, so it over-reports. The
3.5 mm jack J1 is the known false positive: its hull reaches into the wall, the barrel itself (r = 3.0)
sits in an Ø8 hole.

Geometry comes from the KiCad 3D models and is only as good as they are. Board origin: the STL
export uses `--user-origin 60x70mm`, so the board spans x 0..120, y -90..0, bottom face at z = 0.
