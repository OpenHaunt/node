#!/bin/sh
# One Prusa XL plate with two v0.2 enclosures and one v0.1: OH_Node_V0_2x0.2_1x0.1_enclosures.3mf.
# Needs what v0.1/build.sh and v0.2/build.sh need.
set -e
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
r() { openscad --backend Manifold "$@" 2>/dev/null; }
for p in base hood pin; do r -D "part=\"$p\"" -o "$tmp/v0.1 $p.stl" v0.1/enclosure.scad; done
for p in base hood; do r -D "part=\"$p\"" -o "$tmp/v0.2 A $p.stl" v0.2/enclosure.scad; done
r -D 'part="pin"' -D pin_i=0 -o "$tmp/v0.2 A pin BOOT.stl" v0.2/enclosure.scad
r -D 'part="pin"' -D pin_i=1 -o "$tmp/v0.2 A pin RST.stl" v0.2/enclosure.scad
for p in base hood "pin BOOT" "pin RST"; do cp "$tmp/v0.2 A $p.stl" "$tmp/v0.2 B $p.stl"; done
python3 make_3mf.py OH_Node_V0_2x0.2_1x0.1_enclosures.3mf \
    "$tmp/v0.2 A base.stl" "$tmp/v0.2 A hood.stl" "$tmp/v0.2 A pin BOOT.stl,$tmp/v0.2 A pin RST.stl" \
    "$tmp/v0.2 B base.stl" "$tmp/v0.2 B hood.stl" "$tmp/v0.2 B pin BOOT.stl,$tmp/v0.2 B pin RST.stl" \
    "$tmp/v0.1 base.stl" "$tmp/v0.1 hood.stl" "$tmp/v0.1 pin.stl"
python3 check_print.py OH_Node_V0_2x0.2_1x0.1_enclosures.3mf
