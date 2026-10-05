# OH_Node_V0_Prototype — open items before fab

Order: JLCPCB, all SMD assembled, THT by hand (docs/12 "Fabrication"). Relays switch **230 V mains**.
Datasheets in `OH_Node_V0_Prototype/datasheets/`. Closed items are removed from this file; the 2026-10-03
pre-fab review and the first layout review of 2026-10-05 are in its git history. State of 2026-10-05: ERC
clean, DRC 0 errors, 0 unconnected, parity clean.

## Schematic review (2026-10-05)

From the exported netlist and the datasheets. No wrong pin, no open net, no swapped pair. Pinouts of PS1,
D11/D12, Q1/Q2, J3, U1, U2, U6 and U7 were compared with their datasheets, and the docs/12 pin plan matches the
netlist pin for pin.

- [x] Schematic items closed 2026-10-05: R20/R49 10K (Part-DB 597) pull down `/WS_OUT1`/`/WS_OUT2`; J12 pin 2
      is 3V3 instead of GPIO1 (U2.39 NC); no speaker WAGO (D37 amended); power symbols match their rails, and
      the analog rail is renamed `+3V3A` → `+3.3VA` (stock symbol; netclass pattern and board renamed with it);
      docs/12 corrected; datasheets for U7, D9, F1/F2, L2, D10 added. ERC 0.
- [x] Board: R20/R49 placed on F.Cu at (166.3, 124.75) / (167.1, 123.05) on the WS_OUT tracks, GND pad to
      via 0.85 / 0.75 mm. DRC 0, parity clean (2026-10-05).

## Layout review, second round (external feedback, 2026-10-05)

Checked against the board of 2026-10-05 16:31; findings in italics. The screenshots of this round did not come
along with the text.

- [x] One of the two decoupling caps after the PoE module should sit directly behind the module.
      _Done 2026-10-05: C3 moved to (70.1, 110.3), on the 0.6 mm +12V track from PS1.1 to U7, 7.5 mm from PS1.1
      (C2 4.9 mm); GND pad to via 0.95 mm. C4 stays at C35/U7. The /WS_OUT1 reroute to R20 starved U5.3's
      thermal; fixed with a GND track to the via at (167.3, 138.95). DRC 0, parity clean._
- [x] Remove the via in pad at the GND pad of the PoE module; via arrays at the larger GND pads.
      _Done 2026-10-05: via and stub in PS1.2 removed. Fig. 17's keep-out is the mechanical envelope above the
      board, not a via ban; Fig. 12 asks for a via-stitched pour at the output pins (the first-round note was
      wrong). PS1.2 has two rows of four 0.45/0.25 GND vias at y 101.95 and 107.9, 0.25 mm from the pad.
      J7.2, J8.2, J9.2, J10.2: ring of eight 0.45/0.25 GND vias each, 1.75 mm from the pad centre (0.38 mm
      copper to the pad). DRC 0, parity clean._
- [x] Stitching via placement at layer changes can be much better in places: sometimes there is only "0.5"
      of a via per transition, or the via lies beyond another trace. Target: the "bottom right widget board"
      of the reviewer.
      _Done 2026-10-05: goal is at least two GND vias within 2 mm of every signal via, with no cut in its
      reference planes in between. Check: `hardware/tools/return_vias/return_via_check.py <board> [csv]`.
      GND fills In1 and In2, so a F.Cu ↔ B.Cu signal returns over In1/In2 and changes planes through the GND via; only tracks on
      In1/In2 can block that path (the first version of the script counted F.Cu/B.Cu tracks and overstated
      the problem). Bottom right fixed by hand, then seven 0.45/0.25 GND vias added by script at AMP_EN, IO0,
      PIR2, RLY2, WS_OUT1 (2×), WS_OUT2. Now 52 of 53 have two or more; the C30 net (RX centre-tap AC bias,
      116.86/94.8) keeps one at 1.05 mm, no free spot. DRC 0, parity clean.
      List: `analysis/2026-10-05_gnd/return_vias_clear.csv`._
- [x] Rotate L2 by 90°. _Done 2026-10-05, L2 at (102.15, 108.2), rotation 90._

## Bring-up checks

- [ ] USB hot-plug: +5V holds about 176 µF nominal (C21, C34, C36, C37, C27) and the body diode of U7 adds the
      166 µF on +12V (C2, C3, C4, C35). USB allows 10 µF. Watch VBUS on the first plug-in; a weak port may
      droop or trip.
- [ ] Input ripple at U7: C35 is 22 µ/25 V 0805 at 12 V bias, an estimated 5 µF effective against the 10 µF of
      the AP63205 application circuit. Since C3 moved to PS1, C4 (22 µ, 2 mm away) stays at U7 too, so the
      input has about 10 µF effective. Probe +12V at C35 under load.
- [ ] W5500 SPI: SCLK, MOSI, MISO, SCSn are on GPIO9 to 12 and run through the GPIO matrix (the direct FSPI
      pins are GPIO10 to 13 in a different order). Start at 20 MHz. For V1, consider the direct pins.
- [ ] The checks already listed in docs/12: which source carries the load with PoE and USB together; +12V and
      the current into PS1 +VDC with USB only.

## Last step

- [x] **Order files regenerated** 2026-10-05 evening in `production/jlc_final/` after all items above: gerbers
      and CPL from the plugin CLI, BOM from `make_bom.py`. 104 SMD parts, 41 lines (R20/R49 joined the 10K
      line), BOM designators == CPL designators. Against the afternoon set only R20/R49, C3 and L2 changed.
- [x] JLC placement preview checked 2026-10-05: D9 correct (cathode on VIN+), diodes and bridges as marked.
      U2/J11 position and Q1/Q2, U4/U5, U6, U7 rotation fixed with plugin fields (see
      `production/jlc_final/README.md`). C2 and Y1 have no 3D model at JLC: order with "Confirm Parts
      Placement" and the C2 polarity remark from the README.
