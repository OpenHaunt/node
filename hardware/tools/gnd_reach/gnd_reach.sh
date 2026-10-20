#!/usr/bin/env bash
# GND reach map for a KiCad board: how far each point of the GND copper is from the nearest GND via,
# where the pour gets thin, and which SMD GND pads have no via nearby. See README.md.
# Usage: gnd_reach.sh <board.kicad_pcb> [out_dir]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
board="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
out="${2:-$(dirname "$board")/analysis/$(date +%Y-%m-%d)_gnd}"
kpy="${KICAD_PYTHON:-/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3}"
mkdir -p "$out"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
if [ ! -x "$here/.venv/bin/python" ]; then
  python3 -m venv "$here/.venv"
  "$here/.venv/bin/pip" install -q -r "$here/requirements.txt"
fi
# Step 1: KiCad's own python fills the zones and exports copper polygons (reads the board, never writes it)
"$kpy" "$here/export_copper.py" "$board" "$tmp/copper.json" 2>&1 | grep -v -e 'Debug:' -e 'assert' || true
# Step 2: rasterize, flood the GND copper from the vias, render
"$here/.venv/bin/python" "$here/gnd_map.py" "$tmp/copper.json" "$out"
echo "Maps: $out/gnd_FCu.png $out/gnd_BCu.png  Pad list: $out/gnd_pads.json"
