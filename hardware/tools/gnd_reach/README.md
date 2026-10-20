# gnd_reach — where the GND pour is far from a via

Renders, for each outer layer of a KiCad board, how far every point of the GND copper is from the nearest
GND via **measured through the copper**, marks thin sections of the pour, and lists SMD GND pads without a
nearby via. Use it to find pour fingers that only connect at one end ("antennas"), necks in the pour, and
decoupling caps whose GND pad is a long way from the plane.

```bash
hardware/tools/gnd_reach/gnd_reach.sh hardware/OH_Node_V0_Prototype/OH_Node_V0_Prototype.kicad_pcb
# -> hardware/OH_Node_V0_Prototype/analysis/<date>_gnd/gnd_FCu.png, gnd_BCu.png, gnd_pads.json
```

First run creates `.venv/` here with numpy, scipy and pillow. The export step uses KiCad's bundled Python
(`KICAD_PYTHON` overrides the macOS default path). The board file is only read; zones are refilled in memory.
Close KiCad or save first, otherwise the map shows the last saved state.

## Reading the map

| Color | Meaning |
|---|---|
| green → yellow → orange → red → dark red | 0 / 3 / 6 / 10 / ≥ 20 mm through copper to the nearest GND via |
| blue | GND copper with no path to any via (floating island) |
| magenta tint | zone fill thinner than 0.6 mm |
| gray | copper of other nets |
| white rings | GND vias |
| yellow / red circle with label | SMD GND pad, nearest GND via > 1 / > 2 mm away (edge to edge) |

`gnd_pads.json`: per layer, rows of `[distance to via, distance through copper, ref, pad, x, y]`, worst first.

## Assumptions

- Inner layers are solid GND planes, so every GND via and plated GND hole counts as a plane connection.
- Only the GND net is analyzed; other nets are drawn gray for orientation.
- Thresholds (0.6 mm thin, 1 / 2 mm pad distance, color stops) are constants in `gnd_map.py`.
