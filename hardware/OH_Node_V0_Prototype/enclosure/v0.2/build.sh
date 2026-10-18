#!/bin/sh
# Render the parts and write the PrusaSlicer plate OH_Node_V0.2_enclosure.3mf, then check it.
# Needs openscad and python3 with numpy, scipy, networkx, lxml, trimesh, manifold3d.
set -e
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for p in base hood pin; do openscad --backend Manifold -D "part=\"$p\"" -o "$tmp/$p.stl" enclosure.scad; done
python3 ../make_3mf.py OH_Node_V0.2_enclosure.3mf "$tmp/base.stl" "$tmp/hood.stl" "$tmp/pin.stl" 2
python3 ../check_print.py OH_Node_V0.2_enclosure.3mf
