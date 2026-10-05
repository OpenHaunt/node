# V0 enclosure

OpenSCAD enclosure for the V0 prototype board: base, hood and a push pin for SW1. Wall-mount tabs,
hood screwed down with 4 × M2.5 through the board's mounting holes.

| File | |
|---|---|
| `enclosure.scad` | the model; `part` selects what is rendered |
| `stl/base.stl`, `stl/hood.stl`, `stl/pin.stl` | print-ready, already oriented |
| `export_pcb.sh` | exports the populated board as `pcb.stl` (58 MB, gitignored) for the assembly view |
| `check_collision.py` | tests the parts against the board's 3D models |

## Hardware

- 4 × heat-set insert M2.5 × 4 (Ø3.6 bore), pressed into the base bosses from above
- 4 × M2.5 × 25 pan head, from the hood top through post and board into the insert
- Wall: 4 × screws up to 4 mm through the slotted tabs

Set `use_inserts = false` to tap M2.5 directly into the plastic instead (Ø2.2 bore).

## Printing

Base floor down, hood lid down (the LED skin then is the first layer), pin top end down. 0.2 mm layers.
The LED windows over D1/D2 are a 0.4 mm skin and only let light through in a light-coloured or
translucent filament. Set `led_skin = 0` for open holes.

## Assembly

1. Press the inserts into the base.
2. Put the board on the bosses.
3. Drop the pin into the hood's guide from the inside, collar towards the board.
4. Lower the hood: all connector cut-outs are open at the bottom edge, so it slides over the jack barrel.
5. Screw down. The same screws hold the board, so with the hood off the board is loose.

## Rebuilding and checking

```sh
./export_pcb.sh
for p in base hood pin; do openscad --backend Manifold -D "part=\"$p\"" -o stl/$p.stl enclosure.scad; done
# collision check (needs numpy, scipy, trimesh, manifold3d)
for p in base hood pin; do openscad --backend Manifold -D "part=\"${p}_placed\"" -o /tmp/${p}_placed.stl enclosure.scad; done
python3 check_collision.py /tmp pcb.stl
```

The check intersects the convex hull of every 3D-model shell with each part, so it over-reports. The
3.5 mm jack J1 is the known false positive: its hull reaches into the wall, the barrel itself (r = 3.0)
sits in an Ø8 hole.

Geometry comes from the KiCad 3D models and is only as good as they are. Board origin: the STL
export uses `--user-origin 60x70mm`, so the board spans x 0..120, y -90..0, bottom face at z = 0.
