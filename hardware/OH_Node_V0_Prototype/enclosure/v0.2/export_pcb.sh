#!/bin/sh
# Export the populated V0.2 board as pcb.stl in the enclosure's coordinate system.
# Only needed for the assembly view and the collision check; pcb.stl is not committed.
set -e
cd "$(dirname "$0")"
: "${LRV_CUSTOM_LIBS:=/Users/hannes/source/ffp/kicad-custom-libs}"
export LRV_CUSTOM_LIBS
K="${KICAD_CLI:-/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli}"
"$K" pcb export stl -f --user-origin 60x70mm -o pcb.stl ../../OH_Node_V0_Prototype.kicad_pcb
