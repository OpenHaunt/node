// OH Node V0 prototype — 3D-printed enclosure
//
// Coordinates follow `kicad-cli pcb export stl --user-origin 60x70mm`:
// board top-left corner at (0, 0), board spans x 0..120, y -90..0,
// board bottom face at z = 0, top face at z = 1.6.
//
// Parts:
//   base  — floor, board bosses, lower wall with tongue, wall-mount tabs
//   hood  — lid with walls, connector cut-outs, clamp posts, button guide, LED windows
//   pin   — push pin for SW1, printed separately
//
// Assembly: board on the base bosses, pin into the hood guide, hood over the board,
// 4 × M2.5 screws through hood posts and board into heat-set inserts in the base.

part = "assembly";  // [assembly, base, hood, pin, base_placed, hood_placed, pin_placed]
show_pcb = true;    // assembly only: needs pcb.stl from export_pcb.sh
explode = 0;        // assembly only: lift hood by this much

/* [General] */
wall = 2.0;
floor_t = 2.0;
lid_t = 2.0;
gap = 0.5;           // board edge to inner wall
clr = 0.3;           // clearance around connector faces
$fn = 48;

/* [Board] */
pcb_w = 120;
pcb_h = 90;
pcb_r = 3;
pcb_t = 1.6;

// Mounting holes, board coordinates (y negative)
holes = [[4, -4], [3.5, -52.5], [116.5, -86.5], [81.94, -19]];

/* [Heights] */
pin_below = 2.6;     // THT pins below board (RJ45 is deepest, 2.58)
standoff = 4.5;      // board bottom to floor top
tallest = 17.3;      // relay top above board bottom
lid_clear = 1.7;

z_floor = -standoff;
z_split = -1;        // base/hood parting plane
z_lid_in = tallest + lid_clear;
z_top = z_lid_in + lid_t;
tongue = 1.5;        // tongue height above split

/* [Screws and inserts] */
use_inserts = true;
insert_d = 3.6;      // M2.5 heat-set insert, e.g. Ruthex RX-M2.5x4
insert_l = 4;
tap_d = 2.2;         // self-tapping alternative
screw_d = 2.9;       // clearance through the hood post
head_d = 5.0;
boss_d = 6.5;
post_d = 6.0;        // hood clamp post at board
post_d_top = 7.0;

/* [Wall mount] */
tab_len = 12;
tab_w = 16;
tab_t = 3;
tab_slot_d = 4.5;    // for 4 mm screws
tab_slot_len = 6;
tab_y = [-18, -72];  // tab centres along both short sides

/* [Push button SW1] */
sw = [85.26, -31.2];
sw_top = 3.6;        // plunger top above board bottom
pin_d = 4.0;
pin_gap = 0.25;      // pin rest gap above plunger
pin_protrude = 1.0;
guide_d = pin_d + 0.4;
guide_bottom = 11;
collar_d = 6.0;
collar_t = 1.0;

/* [LED windows D1/D2] */
led_skin = 0.4;      // 0 = open hole
led_box = [105.4, -26.0, 111.1, -21.3];   // x0, y0, x1, y1 outer
led_div_x = 108.25;
led_bottom = 4.5;

/* [Vents] */
vents = true;

// ---------------------------------------------------------------------------

xo0 = -gap - wall;           // outer box
yo0 = -pcb_h - gap - wall;
ow = pcb_w + 2 * (gap + wall);
oh = pcb_h + 2 * (gap + wall);
ro = pcb_r + gap + wall;

module rrect(x0, y0, w, h, r) {
    translate([x0 + r, y0 + r]) offset(r = r) square([w - 2 * r, h - 2 * r]);
}
module outline_outer() { rrect(xo0, yo0, ow, oh, ro); }
module outline_inner() { rrect(-gap, -pcb_h - gap, pcb_w + 2 * gap, pcb_h + 2 * gap, pcb_r + gap); }
module outline_mid() { offset(delta = wall / 2) outline_inner(); }

// Box of [x0,x1] × [y0,y1] × [z0,z1]
module box(x0, x1, y0, y1, z0, z1) {
    translate([x0, y0, z0]) cube([x1 - x0, y1 - y0, z1 - z0]);
}

// --- cut-outs through the walls, all open down to the parting plane -------

module cutouts() {
    yt = -gap - 0.1;                 // top wall, y from inner face outward
    yt1 = wall + 1;
    // RJ45 J3 face x 29.84..47.15, z 1.25..14.96
    box(29.84 - clr, 47.15 + clr, yt, yt1, z_split - 5, 14.96 + clr);
    // USB-C J11 centre x 53.44, z 3.25; opening sized for the plug overmold
    usb_w = 10; usb_h = 6.5;
    hull() {
        translate([53.44, yt, 3.25]) rotate([-90, 0, 0])
            linear_extrude(yt1 - yt) offset(r = 1.5) offset(delta = -1.5)
                square([usb_w, usb_h], center = true);
        box(53.44 - usb_w / 2, 53.44 + usb_w / 2, yt, yt1, z_split - 5, z_split);
    }
    // 3.5 mm jack J1 barrel axis x 65.19, z 4.1, barrel Ø6 protrudes 2.3 mm
    jack_d = 8;
    hull() {
        translate([65.19, yt, 4.1]) rotate([-90, 0, 0]) cylinder(d = jack_d, h = yt1 - yt);
        box(65.19 - jack_d / 2, 65.19 + jack_d / 2, yt, yt1, z_split - 5, z_split);
    }
    // Terminal blocks, bottom edge: wire entry faces z 1.6..9.6
    for (x = [6.43, 31.75, 57.06, 82.5])
        box(x - 0.5, x + 15 + 0.5, -pcb_h - gap - wall - 1, -pcb_h + 6, z_split - 5, 10.5);
    // Terminal blocks, right edge J7, J9, J10, J8 merged: y -82.75..-29
    box(pcb_w - 2, pcb_w + gap + wall + 1, -82.75 - 0.5, -29 + 0.5, z_split - 5, 10.5);
}

// --- base ------------------------------------------------------------------

module base() {
    difference() {
        union() {
            // floor
            translate([0, 0, z_floor - floor_t]) linear_extrude(floor_t) outline_outer();
            // outer half of the wall up to the parting plane
            translate([0, 0, z_floor - 0.01])
                linear_extrude(z_split - z_floor + 0.01)
                    difference() { outline_outer(); outline_inner(); }
            // tongue: inner half rises above the parting plane
            translate([0, 0, z_split - 0.01])
                linear_extrude(tongue + 0.01)
                    difference() { outline_mid(); outline_inner(); }
            // board bosses
            for (h = holes) translate([h[0], h[1], z_floor - 0.01])
                cylinder(d = boss_d, h = standoff + 0.01);
            // wall-mount tabs
            for (side = [0, 1], y = tab_y) tab(side, y);
        }
        cutouts();
        for (h = holes) translate([h[0], h[1], 0]) {
            // the bore runs into the floor so a standard M2.5x25 does not bottom out
            if (use_inserts) translate([0, 0, z_floor - floor_t + 0.6]) cylinder(d = insert_d, h = standoff + floor_t);
            else translate([0, 0, -standoff]) cylinder(d = tap_d, h = standoff + 1);
        }
    }
}

module tab(side, y) {
    x_edge = side == 0 ? xo0 : xo0 + ow;
    dir = side == 0 ? -1 : 1;
    z0 = z_floor - floor_t;
    difference() {
        union() {
            hull() {
                translate([x_edge - dir * 1, y - tab_w / 2, z0]) cube([0.01 + 1, tab_w, tab_t]);
                translate([x_edge + dir * (tab_len - tab_w / 2), y, z0]) cylinder(d = tab_w, h = tab_t);
            }
            // gusset
            hull() {
                translate([x_edge - dir * 0.5 - 0.5, y - 1.5, z0]) cube([1, 3, z_split - z0]);
                translate([x_edge + dir * 4 - 0.5, y - 1.5, z0]) cube([1, 3, tab_t]);
            }
        }
        translate([x_edge + dir * (tab_len - tab_w / 2), y, z0 - 1])
            hull() for (dx = [0, -dir * tab_slot_len]) translate([dx, 0, 0]) cylinder(d = tab_slot_d, h = tab_t + 2);
    }
}

// --- hood ------------------------------------------------------------------

screws_bottom = [for (x0 = [8.93, 34.25, 59.56, 85.0]) [x0, -80, 3, 0]];   // [x, y, poles, along]
screws_right = [[114.75, -41.5, 3, 1], [114.75, -57.75, 3, 1], [114.75, -69, 2, 1], [114.75, -80.25, 2, 1]];

module screw_window(s) {
    n = s[2];
    w = 5.0;
    len = (n - 1) * 5;
    translate([s[0], s[1], z_lid_in - 1])
        hull() for (i = [0, n - 1])
            translate(s[3] == 0 ? [i * 5, 0, 0] : [0, i * 5, 0]) cylinder(d = w, h = lid_t + 2);
}

module hood() {
    difference() {
        union() {
            // lid plate
            translate([0, 0, z_lid_in]) linear_extrude(lid_t) outline_outer();
            // walls: outer half from the parting plane, inner half above the tongue
            translate([0, 0, z_split]) linear_extrude(z_lid_in - z_split + 0.01)
                difference() { outline_outer(); outline_mid(); }
            translate([0, 0, z_split + tongue]) linear_extrude(z_lid_in - z_split - tongue + 0.01)
                difference() { outline_mid(); outline_inner(); }
            // clamp posts
            for (h = holes) translate([h[0], h[1], pcb_t]) {
                cylinder(d = post_d, h = 3);
                translate([0, 0, 2.99]) cylinder(d1 = post_d, d2 = post_d_top, h = z_lid_in - pcb_t - 3 + 0.02);
            }
            // button guide
            translate([sw[0], sw[1], guide_bottom]) cylinder(d = guide_d + 2.4, h = z_lid_in - guide_bottom + 0.01);
            // LED light box
            box(led_box[0], led_box[2], led_box[1], led_box[3], led_bottom, z_lid_in + 0.01);
        }
        cutouts();
        // screw holes and head seats
        for (h = holes) translate([h[0], h[1], 0]) {
            cylinder(d = screw_d, h = 100);
            translate([0, 0, z_top - 0.6]) cylinder(d = head_d + 0.4, h = 1);
        }
        // button guide bore
        translate([sw[0], sw[1], 0]) cylinder(d = guide_d, h = 100);
        // LED chambers, separated by a divider, closed by a thin skin on top
        led_top = z_top - led_skin - (led_skin == 0 ? 1 : 0);
        for (c = [[led_box[0] + 0.8, led_div_x - 0.4], [led_div_x + 0.4, led_box[2] - 0.8]])
            box(c[0], c[1], led_box[1] + 0.8, led_box[3] - 0.8, led_bottom - 1, led_top);
        // screwdriver windows over the terminal screws
        for (s = concat(screws_bottom, screws_right)) screw_window(s);
        if (vents) vents_cut();
    }
}

module vents_cut() {
    // lid grille over the PoE module PS1 (x 2..23, y -36..-22)
    for (i = [0:5]) translate([4 + i * 3.4, -37, z_lid_in - 1]) cube([1.6, 16, lid_t + 2]);
    // left wall, upper half
    for (i = [0:7]) translate([xo0 - 1, -80 + i * 8.5, 8]) cube([wall + 2, 2, 8]);
}

// --- push pin ----------------------------------------------------------------

pin_bottom = sw_top + pin_gap;
pin_len = z_top + pin_protrude - pin_bottom;

module pin() {
    // modelled in place; for printing it is laid on its collar side in print_pin()
    translate([sw[0], sw[1], pin_bottom]) {
        cylinder(d = 2.5, h = 1.01);
        translate([0, 0, 1]) cylinder(d = pin_d, h = pin_len - 1);
        translate([0, 0, guide_bottom - pin_bottom - 0.5 - collar_t]) cylinder(d = collar_d, h = collar_t);
    }
}

// --- print layout ------------------------------------------------------------

module print_hood() { translate([0, 0, z_top]) rotate([180, 0, 0]) hood(); }
module print_base() { translate([0, 0, -(z_floor - floor_t)]) base(); }
// top end on the bed, so the collar's overhang is only its 1 mm ring
module print_pin() { translate([0, 0, z_top + pin_protrude]) rotate([180, 0, 0]) translate([-sw[0], -sw[1], 0]) pin(); }

module pcb() { color("darkgreen", 0.9) import("pcb.stl", convexity = 10); }

echo(str("Outer size: ", ow, " x ", oh, " x ", z_top - (z_floor - floor_t), " mm (without tabs)"));
screw_seat = z_top - 0.6;
echo(str("Screw: M2.5, length ", screw_seat - (-insert_l + 1), "..", screw_seat - (z_floor - floor_t + 0.6), " mm (min. 1 mm into insert / max. hole depth)"));
echo(str("Pin length: ", pin_len, " mm"));

if (part == "assembly") {
    color("lightgray") base();
    color("steelblue", 0.6) translate([0, 0, explode]) hood();
    color("orange") translate([0, 0, explode]) pin();
    if (show_pcb) pcb();
} else if (part == "base") print_base();
else if (part == "hood") print_hood();
else if (part == "pin") print_pin();
// in assembly position, for check_collision.py
else if (part == "base_placed") base();
else if (part == "hood_placed") hood();
else if (part == "pin_placed") pin();
