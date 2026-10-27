// OH Node V0.2 prototype — 3D-printed enclosure
//
// Derived from ../v0.1. Same outline, mounting holes, terminals, relays and
// base; what moved is the top edge (USB-C, RJ45, line-out jack, speaker terminal J13), plus
// the reset button SW2, six status LEDs and the J12 expansion header.
//
// Coordinates follow `kicad-cli pcb export stl --user-origin 60x70mm`:
// board top-left corner at (0, 0), board spans x 0..120, y -90..0,
// board bottom face at z = 0, top face at z = 1.6.
//
// Parts:
//   base  — floor, board bosses, lower wall with tongue, wall-mount tabs
//   hood  — lid with walls, connector cut-outs, clamp posts, button guides, LED windows
//   pin   — push pin for SW1 and SW2, printed twice
//
// Assembly: board on the base bosses, pins into the hood guides, hood over the board,
// 4 × M2.5 screws through hood posts and board into heat-set inserts in the base.

part = "assembly";  // [assembly, base, hood, pin, base_placed, hood_placed, pin_placed, screws_placed]
pin_i = 0;          // part="pin": which button, index into sws
show_pcb = true;    // assembly only: needs pcb.stl from export_pcb.sh
explode = 0;        // assembly only: lift hood by this much
show_hardware = true;   // assembly only: screws and inserts

/* [General] */
wall = 2.0;
floor_t = 2.0;
lid_t = 2.5;         // room for a flush screw head seat
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
standoff = 5.7;      // board bottom to floor top; leaves a closed 1.0 floor under the insert bore
tallest = 17.3;      // relay top above board bottom
lid_clear = 1.7;

z_floor = -standoff;
z_split = -1;        // base/hood parting plane
z_lid_in = tallest + lid_clear;
z_top = z_lid_in + lid_t;
tongue = 1.5;        // tongue height above split

/* [Screws and inserts] */
use_inserts = true;
insert_d = 4.0;      // CNC Kitchen M2.5 x 4.0 heat-set insert: hole Ø4.0, wall min. 1.6
insert_l = 4.0;
insert_depth = 6.7;  // blind hole, closed at the bottom: room below the insert for the screw tip;
                     // also the minimum depth for a ruthex RX-M2.5x5.7, which fits the same hole
insert_od = 4.6;
tap_d = 2.2;         // self-tapping alternative
screw_d = 2.9;       // clearance through the hood post
countersunk = true;  // DIN 965 M2.5 x 25 (Part-DB 285) in a 90° countersink; false = ISO 14580 cheese head
head_d = 5.0;        // cheese head seat is head_d + 0.4
head_k = 1.85;       // ISO 14580 M2.5 head height
seat_depth = head_k + 0.15;   // cheese head sits just below the lid surface
csk_dk = 4.7;        // DIN 965 M2.5 head Ø, actual max.
csk_d = csk_dk + 0.5;         // countersink Ø at the lid surface; the head top sits 0.25 below it
boss_d = 7.5;        // insert_d + 2 × 1.75
post_d = 6.0;        // hood clamp post at board
post_d_top = 7.0;
// SMD parts under a post foot, x0, x1, y0, y1: R50 beside H4
post_reliefs = [[2.42, 3.42, -49.6, -49.1]];

/* [Wall mount] */
tab_len = 16;        // from the outer wall
tab_w = 20;
tab_t = 4;
tab_slot_d = 4.5;    // clearance for 4 mm wood screws or M4
tab_slot_len = 6;    // slot along the tab, center to center
tab_slot_x0 = 4.5;   // inner slot center from the wall: room for a Ø8 screw head
gusset_w = 2.5;
tab_y = [-18, -72];  // tab centers along both short sides

/* [Push buttons SW1 (BOOT), SW2 (RST)] */
sws = [[85.26, -31.21], [96.68, -31.26]];
sw_labels = ["BOOT", "RST"];
sw_top = 3.6;        // plunger top above board bottom
pin_d = 4.0;
pin_gap = 0.25;      // pin rest gap above plunger
pin_protrude = [2.0, 1.0];   // per button; BOOT 1.0 until 2026-10-26
guide_d = pin_d + 0.4;
guide_bottom = 11;
collar_d = 6.0;
collar_t = 1.0;

/* [Labels] */
labels = true;       // engraved into the lid top
label_depth = 0.4;
label_size = 3;

/* [USB-C J11] */
usb_w = 13.0;        // opening for the plug overmold: USB-C allows 12.35 x 6.5, some cables are wider
usb_h = 8.0;

/* [LED windows] */
led_skin = 0;        // 0 = open hole; 0.4 = skin that lets light through a light filament
// D1/D2: one box with a divider
led_box = [105.4, -26.0, 111.1, -21.3];   // x0, y0, x1, y1 outer
led_div_x = 108.25;
led_bottom = 4.5;
// status LEDs, one chamber each: relays D3, D4, D14, D15, PIRs D16, D17
status_leds = [[22.99, -51.05], [48.3, -51.05], [73.6, -51.05], [99.01, -51.05], [108.38, -29.6], [108.88, -45.8]];
status_box = 3.6;    // outer size, 0.8 walls

/* [J12 expansion header] */
j12_slot = true;     // lid slot for the cable of a plug-on 1×8 socket
j12 = [117.43, -17.51];   // header center
j12_slot_size = [3.4, 21.2];

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

// --- cut-outs through the walls, all open down to zb ------------------------
// The hood needs them open to below the parting plane so it slides over the connectors.
// The base cuts them only from the parting plane up: nothing reaches below the board,
// and its wall then stays whole under the terminals and carries the wall-mount tabs.

module cutouts(zb = z_split - 5) {
    yt = -gap - 0.1;                 // top wall, y from inner face outward
    yt1 = wall + 1;
    // USB-C J11 center x 12.41, z 3.25; opening sized for the plug overmold
    hull() {
        translate([12.41, yt, 3.25]) rotate([-90, 0, 0])
            linear_extrude(yt1 - yt) offset(r = 1.5) offset(delta = -1.5)
                square([usb_w, usb_h], center = true);
        box(12.41 - usb_w / 2, 12.41 + usb_w / 2, yt, yt1, zb, z_split + 0.01);
    }
    // RJ45 J3 face x 23.89..41.20, z 1.25..14.96
    box(23.89 - clr, 41.20 + clr, yt, yt1, zb, 14.96 + clr);
    // line-out jack J1 (Kycon STX-3000): front collar Ø6 axis x 50.7, z 4.6, front face
    // 0.6 inside the outer wall, so the opening is sized for the plug overmold
    jack_d = 9;
    hull() {
        translate([50.7, yt, 4.6]) rotate([-90, 0, 0]) cylinder(d = jack_d, h = yt1 - yt);
        box(50.7 - jack_d / 2, 50.7 + jack_d / 2, yt, yt1, zb, z_split + 0.01);
    }
    // speaker terminal J13, wire entry towards the top edge
    box(60.9 - 0.5, 70.9 + 0.5, yt, yt1, zb, 10.5);
    // Terminal blocks, bottom edge: wire entry faces z 1.6..9.6
    for (x = [6.43, 31.75, 57.06, 82.5])
        box(x - 0.5, x + 15 + 0.5, -pcb_h - gap - wall - 1, -pcb_h + 6, zb, 10.5);
    // Terminal blocks, right edge J7, J9, J10, J8 merged: y -82.75..-29
    box(pcb_w - 2, pcb_w + gap + wall + 1, -82.75 - 0.5, -29 + 0.5, zb, 10.5);
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
            // board bosses, kept out of the hood's half of the wall in the corners
            intersection() {
                for (h = holes) translate([h[0], h[1], z_floor - 0.01])
                    cylinder(d = boss_d, h = standoff + 0.01);
                translate([0, 0, z_floor - 1]) linear_extrude(standoff + 2) outline_mid();
            }
            // wall-mount tabs
            for (side = [0, 1], y = tab_y) tab(side, y);
        }
        cutouts(z_split);
        for (h = holes) translate([h[0], h[1], 0]) {
            // the bore runs into the floor so a standard M2.5x25 does not bottom out
            if (use_inserts) translate([0, 0, -insert_depth]) cylinder(d = insert_d, h = insert_depth + 1);
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
            // gussets flush with both tab edges, running out where the tab starts to round off
            reach = tab_len - tab_w / 2;
            for (gy = [-1, 1] * (tab_w / 2 - gusset_w / 2)) hull() {
                translate([x_edge - dir * 0.5 - 0.5, y + gy - gusset_w / 2, z0]) cube([1, gusset_w, z_split - z0]);
                translate([x_edge + dir * (reach - 0.5) - 0.5, y + gy - gusset_w / 2, z0]) cube([1, gusset_w, tab_t]);
            }
        }
        translate([x_edge, y, z0 - 1])
            hull() for (dx = [tab_slot_x0, tab_slot_x0 + tab_slot_len]) translate([dir * dx, 0, 0]) cylinder(d = tab_slot_d, h = tab_t + 2);
    }
}

// --- hood ------------------------------------------------------------------

screws_bottom = [for (x0 = [8.93, 34.25, 59.56, 85.0]) [x0, -80, 3, 0]];   // [x, y, poles, along]
screws_right = [[114.75, -41.5, 3, 1], [114.75, -57.75, 3, 1], [114.75, -69, 2, 1], [114.75, -80.25, 2, 1]];
screws_top = [[63.4, -4.6, 2, 0]];   // J13

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
            // button guides
            for (s = sws) translate([s[0], s[1], guide_bottom]) cylinder(d = guide_d + 2.4, h = z_lid_in - guide_bottom + 0.01);
            // LED light boxes
            box(led_box[0], led_box[2], led_box[1], led_box[3], led_bottom, z_lid_in + 0.01);
            for (l = status_leds)
                box(l[0] - status_box / 2, l[0] + status_box / 2, l[1] - status_box / 2, l[1] + status_box / 2, led_bottom, z_lid_in + 0.01);
        }
        cutouts();
        // screw holes and head seats
        for (h = holes) translate([h[0], h[1], 0]) {
            cylinder(d = screw_d, h = 100);
            if (countersunk)
                translate([0, 0, z_top - (csk_d - screw_d) / 2])
                    cylinder(d1 = screw_d, d2 = csk_d + 2, h = (csk_d - screw_d) / 2 + 1);
            else
                translate([0, 0, z_top - seat_depth]) cylinder(d = head_d + 0.4, h = seat_depth + 1);
        }
        for (r = post_reliefs) box(r[0] - 0.3, r[1] + 0.3, r[2] - 0.3, r[3] + 0.3, pcb_t - 1, pcb_t + 0.8);
        // button guide bores
        for (s = sws) translate([s[0], s[1], 0]) cylinder(d = guide_d, h = 100);
        // LED chambers, open or closed by a thin skin on top
        led_top = led_skin == 0 ? z_top + 1 : z_top - led_skin;
        for (c = [[led_box[0] + 0.8, led_div_x - 0.4], [led_div_x + 0.4, led_box[2] - 0.8]])
            box(c[0], c[1], led_box[1] + 0.8, led_box[3] - 0.8, led_bottom - 1, led_top);
        for (l = status_leds) let(r = status_box / 2 - 0.8)
            box(l[0] - r, l[0] + r, l[1] - r, l[1] + r, led_bottom - 1, led_top);
        // screwdriver windows over the terminal screws
        for (s = concat(screws_bottom, screws_right, screws_top)) screw_window(s);
        if (j12_slot) translate([j12[0], j12[1], z_lid_in - 1])
            linear_extrude(lid_t + 2) offset(r = 1) offset(delta = -1) square(j12_slot_size, center = true);
        if (labels) for (i = [0:len(sws) - 1])
            translate([sws[i][0], sws[i][1] - 6, z_top - label_depth]) linear_extrude(1)
                text(sw_labels[i], size = label_size, font = "Liberation Sans:style=Bold", halign = "center", valign = "center");
        if (vents) vents_cut();
    }
}

module vents_cut() {
    // lid grille over the PoE module PS1 (x 1.2..17.7, y -44.8..-20.8)
    for (i = [0:4]) translate([3 + i * 3.4, -42, z_lid_in - 1]) cube([1.6, 18, lid_t + 2]);
    // left wall, upper half
    for (i = [0:7]) translate([xo0 - 1, -80 + i * 8.5, 8]) cube([wall + 2, 2, 8]);
}

// --- push pin ----------------------------------------------------------------

pin_bottom = sw_top + pin_gap;
function pin_len(i) = z_top + pin_protrude[i] - pin_bottom;

module pin(i = 0) {
    // modeled in place; for printing it is turned top end down in print_pin()
    s = sws[i];
    translate([s[0], s[1], pin_bottom]) {
        cylinder(d = 2.5, h = 1.01);
        translate([0, 0, 1]) cylinder(d = pin_d, h = pin_len(i) - 1);
        // collar with a 45° cone towards the top, so printed top end down it has no flat overhang;
        // the cone stops against the guide bore where it is guide_d wide, 0.5 below the guide
        translate([0, 0, guide_bottom - pin_bottom - 0.5 - (collar_d - guide_d) / 2 - collar_t]) {
            cylinder(d = collar_d, h = collar_t);
            translate([0, 0, collar_t - 0.01]) cylinder(d1 = collar_d, d2 = pin_d, h = (collar_d - pin_d) / 2 + 0.01);
        }
    }
}

// --- print layout ------------------------------------------------------------

module print_hood() { translate([0, 0, z_top]) rotate([180, 0, 0]) hood(); }
module print_base() { translate([0, 0, -(z_floor - floor_t)]) base(); }
// top end on the bed; make_3mf.py gives the pins a brim, their footprint is only Ø4
module print_pin(i) { translate([0, 0, z_top + pin_protrude[i]]) rotate([180, 0, 0]) translate([-sws[i][0], -sws[i][1], 0]) pin(i); }

module pcb() { color("darkgreen", 0.9) import("pcb.stl", convexity = 10); }

// --- hardware, assembly view only --------------------------------------------
// DIN 965 M2.5 x 25 countersunk (length includes the head) in the lid countersink, or
// ISO 14580 M2.5 x 25 (CNC Kitchen, head Ø4.5 x 1.85) on the flat seat;
// CNC Kitchen M2.5 x 4.0 insert (Ø4.6) flush with the boss top.

screw_l = 25;
screw_head = [4.5, head_k];
csk_k = (csk_dk - 2.5) / 2;   // 90° head, 1.1 high

module screw() {
    if (countersunk) translate([0, 0, screw_seat]) {
        translate([0, 0, -csk_k]) cylinder(d1 = 2.5, d2 = csk_dk, h = csk_k);
        translate([0, 0, -screw_l]) cylinder(d = 2.5, h = screw_l - csk_k + 0.01);
    } else translate([0, 0, screw_seat]) {
        difference() {
            cylinder(d = screw_head[0], h = screw_head[1]);
            translate([0, 0, screw_head[1] - 1]) cylinder(d = 1.8, h = 2, $fn = 6);
        }
        translate([0, 0, -screw_l]) cylinder(d = 2.5, h = screw_l + 0.01);
    }
}
module insert() {
    translate([0, 0, -insert_l]) difference() {
        cylinder(d = insert_od, h = insert_l);
        translate([0, 0, -1]) cylinder(d = 2.5, h = insert_l + 2);
    }
}

echo(str("Outer size: ", ow, " x ", oh, " x ", z_top - (z_floor - floor_t), " mm (without tabs)"));
// countersunk: head top; cheese head: head bottom
screw_seat = countersunk ? z_top - (csk_d - csk_dk) / 2 : z_top - seat_depth;
echo(str("Screw: M2.5, length ", screw_seat + insert_l - 1.5, "..", screw_seat + insert_depth - 0.3, " mm",
    countersunk ? " including the head" : " under the head",
    " (at least insert length - 1.5 engaged / short of the bore bottom)"));
echo(str("Screw tip ", screw_seat - screw_l, " (insert -", insert_l, "..0, bore bottom -", insert_depth, ")"));
echo(str("Floor under the insert bore: ", standoff + floor_t - insert_depth, " mm"));
assert(!use_inserts || standoff + floor_t - insert_depth >= 0.8, "insert bore breaks through the floor");
for (i = [0:len(sws) - 1]) echo(str("Pin ", sw_labels[i], " length: ", pin_len(i), " mm"));

if (part == "assembly") {
    color("lightgray") base();
    color("steelblue", 0.6) translate([0, 0, explode]) hood();
    color("orange") translate([0, 0, explode]) for (i = [0:len(sws) - 1]) pin(i);
    if (show_pcb) pcb();
    if (show_hardware) for (h = holes) translate([h[0], h[1], 0]) {
        color("silver") translate([0, 0, explode]) screw();
        if (use_inserts) color("goldenrod") insert();
    }
} else if (part == "base") print_base();
else if (part == "hood") print_hood();
else if (part == "pin") print_pin(pin_i);
// in assembly position, for check_collision.py
else if (part == "base_placed") base();
else if (part == "hood_placed") hood();
else if (part == "screws_placed") for (h = holes) translate([h[0], h[1], 0]) screw();
else if (part == "pin_placed") for (i = [0:len(sws) - 1]) pin(i);
