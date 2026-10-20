# V0.2 enclosure

OpenSCAD enclosure for the V0.2 board: base, hood and two push pins for SW1 (BOOT) and SW2 (RST).
Derived from the V0.1 enclosure in `../v0.1`: same outline, mounting holes, height, wall-mount
tabs and screws. It differs where the board does:

| | V0.1 | V0.2 |
|---|---|---|
| USB-C J11 | x 53.4 | x 12.4 |
| RJ45 J3 | x 29.8..47.2 | x 23.9..41.2 |
| Jack J1 | SJ1-3523N, barrel outside the wall, Ø8 hole | STX-3000, Ø6 collar ends 0.6 mm inside the wall, Ø9 hole for the plug overmold |
| Speaker terminal J13 | — | top edge, wire entry and screwdriver window |
| Buttons | SW1 | SW1 and SW2, engraved BOOT / RST |
| LED windows | D1/D2 | D1/D2, relay LEDs D3/D4/D14/D15, PIR LEDs D16/D17 |
| J12 header | — | lid slot for the cable of a plugged-on 1 × 8 socket (`j12_slot = false` closes it) |
| Lid vents | over PS1 at x 4..23 | over PS1 at x 3..18 |

The H4 clamp post has a small relief in its foot for R50.

| File | |
|---|---|
| `enclosure.scad` | the model; `part` selects what is rendered. The assembly view also shows screws and inserts (`show_hardware`); `part="screws_placed"` exports the screws for the collision check |
| `OH_Node_V0.2_enclosure.3mf` | PrusaSlicer plate: base, hood and both pins as separate named objects, already oriented, laid out for the Prusa XL |
| `build.sh` | renders the parts, writes the 3MF and runs the print check |
| `export_pcb.sh` | exports the populated board as `pcb.stl` (64 MB, gitignored) for the assembly view |

The collision check, the print check and the 3MF writer are shared with V0.1: `../check_collision.py`,
`../check_print.py`, `../make_3mf.py`.

## Parts

Per enclosure. Part-DB status as of 2026-10-18. Inserts and screws both from 3DJake (3djake.de, ships from Austria).

| Qty | Part | Notes | Part-DB |
|---|---|---|---|
| 4 | Heat-set insert CNC Kitchen M2.5 Standard, M2.5 × 4.0 ([3DJake](https://www.3djake.de/cnc-kitchen/gewindeeinsatz-m25-standard), 100 pcs) | hole Ø4.0, blind, 6.7 deep with 1.0 closed floor below; the screw tip ends 1.2 above the bottom; min. wall 1.6 (boss Ø7.5). A ruthex RX-M2.5x5.7 fits the same hole | not yet, to order |
| 4 | Low cheese head screw ISO 14580 M2.5 × 25 A2-70, Torx T8 (CNC Kitchen, 25 pcs, [3DJake](https://www.3djake.de/cnc-kitchen/m25-zylinderschrauben-niedriger-kopf-iso-14580-edelstahl-a2-70), variant M2,5x25; ISO 14583 pan head fits too) | from the hood top through post and board into the insert; head Ø4.5 × 1.85 in a Ø5.4 seat 2.0 deep, so it sits 0.15 below the lid surface. Length 23.7–25.9 works: the 25 runs through the whole insert and ends 1.2 above the bore bottom | not yet, to order; ID 354 is the same screw at 12 mm (Bossard) |
| 4 | Wall screw: 4 mm wood screw with wall plug, or M4; pan or flat head Ø8–9 | through the 4.5 × 10.5 slots of the 4 mm thick tabs (16 × 20, gussets on both edges); not countersunk. Optional washer DIN 125 4.3 | washer: ID 286, only 2 in stock |
| 2 | Push pin | printed, in `OH_Node_V0.2_enclosure.3mf` | — |
| ~85 g | PETG, light or translucent | solid volume of all parts about 67 cm³; the LED skins need a light color. In stock: Prusament PETG Signal White (ID 304), Clear (ID 294) | in stock |
| 1 (opt.) | Socket for J12: 2 × WR-WTB 4P female housing 61900411621 + 8 crimp contacts 61900113722DEC | there is no 8P housing in stock; two 4P side by side; the cable leaves through the lid slot | IDs 96 (9 in stock), 99 (150 in stock) |

Tools: soldering iron with an insert tip (inserts at 10–20 °C above the print temperature), Torx T8.

Set `use_inserts = false` to tap M2.5 directly into the plastic instead (Ø2.2 bore).

## Printing

Prusa XL: open `OH_Node_V0.2_enclosure.3mf` in PrusaSlicer, the parts are already oriented and on the plate (any printer with a 150 × 95 mm bed works part by part). Base floor down, hood lid down (the LED skins and the
engraved labels are then on the first layer), pins top end down. 0.2 mm layers, no supports.
The 3MF gives the pins a 4 mm brim (they stand 18.65 mm tall on a Ø4 end); their collar has a 45° cone
on the bed side instead of a flat overhang. Sliced in PrusaSlicer 2.9.6 with 0.4 nozzle, 2 perimeters,
15 % infill, PETG: no stability warnings, about 73 g and 8 h 3 min.
The LED windows are a 0.4 mm skin and only let light through in a light-colored or translucent
filament. Set `led_skin = 0` for open holes.

`check_print.py` fails a part that is not one closed solid: a second body (something floating, or
joined only by a face or an edge), non-manifold edges, or a part not lying on the bed.

The lid is 2.5 mm thick (2.0 until 2026-10-18) so the screw heads can sit flush; the hood is 0.5 mm taller, nothing inside moved.
The board sits 5.7 mm above the floor (4.5 until 2026-10-18), so the 6.7 mm insert bore stays closed at the bottom;
only the base walls grew, by 1.2 mm. Outer height 29.2 mm without tabs.

## Assembly

1. Press the inserts into the base, flush with the boss top (the bore is deeper than the insert, room for the screw tip).
2. Put the board on the bosses. A socket for J12 goes on now, its cable through the lid slot.
3. Drop the pins into the hood's guides from the inside, collar towards the board.
4. Lower the hood: all connector cut-outs are open at the bottom edge.
5. Screw down. The same screws hold the board, so with the hood off the board is loose.

## Rebuilding and checking

```sh
./build.sh                # → OH_Node_V0.2_enclosure.3mf, with the print check
./export_pcb.sh           # board for the assembly view and the collision check
# needs numpy, scipy, networkx, lxml, trimesh, manifold3d
for p in base hood pin; do openscad --backend Manifold -D "part=\"${p}_placed\"" -o /tmp/${p}_placed.stl enclosure.scad; done
python3 ../check_collision.py /tmp pcb.stl
```

The collision check intersects the convex hull of every 3D-model shell with each part, so it over-reports;
for V0.2 it reports nothing. It does not check that a plug fits through an opening.

Geometry comes from the KiCad 3D models and is only as good as they are. Board origin: the STL
export uses `--user-origin 60x70mm`, so the board spans x 0..120, y -90..0, bottom face at z = 0.
