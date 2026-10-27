#!/bin/sh
# One Prusa XL plate with three v0.2 enclosures: OH_Node_V0_3x0.2_enclosures.3mf.
# Needs what v0.2/build.sh needs.
set -e
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
r() { openscad --backend Manifold "$@" 2>/dev/null; }
for p in base hood; do r -D "part=\"$p\"" -o "$tmp/v0.2 A $p.stl" v0.2/enclosure.scad; done
r -D 'part="pin"' -D pin_i=0 -o "$tmp/v0.2 A pin BOOT.stl" v0.2/enclosure.scad
r -D 'part="pin"' -D pin_i=1 -o "$tmp/v0.2 A pin RST.stl" v0.2/enclosure.scad
for c in B C; do for p in base hood "pin BOOT" "pin RST"; do cp "$tmp/v0.2 A $p.stl" "$tmp/v0.2 $c $p.stl"; done; done
set --
for c in A B C; do set -- "$@" "$tmp/v0.2 $c base.stl" "$tmp/v0.2 $c hood.stl" "$tmp/v0.2 $c pin BOOT.stl,$tmp/v0.2 $c pin RST.stl"; done
python3 make_3mf.py OH_Node_V0_3x0.2_enclosures.3mf "$@"
python3 check_print.py OH_Node_V0_3x0.2_enclosures.3mf
