#!/bin/sh
# Export the populated V0.1 board as pcb.stl in the enclosure's coordinate system.
# Only needed for the assembly view and the collision check; pcb.stl is not committed.
# The board file now holds V0.2, so the V0.1 layout is taken from the v0.1 tag.
set -e
cd "$(dirname "$0")"
: "${LRV_CUSTOM_LIBS:=/Users/hannes/source/ffp/kicad-custom-libs}"
export LRV_CUSTOM_LIBS
K="${KICAD_CLI:-/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git show v0.1:./../../OH_Node_V0_Prototype.kicad_pcb > "$tmp/OH_Node_V0_Prototype.kicad_pcb"
"$K" pcb export stl -f --user-origin 60x70mm -o pcb.stl "$tmp/OH_Node_V0_Prototype.kicad_pcb"
