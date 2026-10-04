# OH_Node_V0_Prototype — pre-fab review 2026-10-03

Order: JLCPCB, all SMD assembled, THT by hand (docs/12 "Fabrication"). Relays switch **230 V mains**.
Evidence: `OH_Node_V0_Prototype/analysis/2026-10-03_1454-3/` (analyzer JSON), datasheets in `datasheets/`.
State checked: schematic of 2026-10-01 18:45, board of 2026-10-03 14:32. KiCad ERC/DRC clean (0 errors,
0 unconnected, parity clean, also with zones refilled); everything from the 2026-09-29 review is closed
except the items carried over under Minor.

## Blockers

- [x] **J1 audio jack slots too narrow.** Stock footprint `Jack_3.5mm_CUI_SJ1-3523N_Horizontal` drills
      0.4 × 1.4 mm slots; CUI's recommended layout is 1.5 × 0.8 mm, and JLC's minimum plated slot width is
      0.5 mm. Copy the footprint into LRV with 0.8 × 1.5 mm slots and larger pads (about 1.6 × 2.4 mm).
      _2026-10-04:_ `LRV:Jack_3.5mm_CUI_SJ1-3523N_Horizontal` created (0.8 × 1.5 slots, 1.6 × 2.4 pads,
      3D model in LRV, BOM/pos exclusion removed). DRC with the larger pads at J1's position: 0 errors.
      Part-DB 581 and J1 on the board now use it; DRC 0 errors, 0 unconnected, parity clean.

## Major

- [x] **W5500 digital VDD (pin 28) has no local decoupling.** The only +3V3 caps near U1, C14 (1 µ) and C15
      (100 n), sit at the ferrite on the other side of the chip: 24–25 mm of 0.25 mm track to pin 28.
      Move C15 to pins 28/29 (between U1 and Y1), or add a 100 n there.
      _2026-10-04:_ after the reroute pin 28 is fed from the right, straight from U3's tab (24 mm); C14 is
      24 mm the other way, no +3V3 plane. Plan: C15 to about (131.7, 100.9), horizontal, +3V3 pad on the
      x = 132.16 track (2.7 mm from pin 28), GND pad into the pour at U1's corner plus a via.
      Done: C15 at (131.8, 99.55), vertical, +3V3 pad 1.9 mm from pin 28; GND pad on a 1.1 mm track to a
      via at (131.4, 100.95) into the inner GND planes. DRC 0 errors, 0 unconnected, parity clean.
- [x] **PS1 has the reflow land pattern but is hand-soldered.** Pads are 3.81 mm long and end at the module
      edge (Ag9900 datasheet Fig. 21: G 16.59, D 15.19, E 17.53). The hand-soldering pattern (Fig. 22) uses
      5.00 mm pads, G 17.88, D 15.85, E 18.19, about 1.2 mm visible outside the module. Change the
      footprint, or solder with paste and hot air.
      _2026-10-04:_ `LRV:AG9900-MTB_HandSolder` created (Fig. 22: 5.00 mm pads, pads 1/4/5 0.66 mm further
      out, pads 2/3 centre 0.32 mm off the pad-1 line; module body, keep-out and 3D unchanged). DRC with
      these pads at PS1: +5V 1.5 mm track at y 108.325 is 0.145 mm from pad 2 (POWER needs 0.15), and pad 2
      gets only one thermal spoke. Silk/fab recentred on the body (21.21 × 13.92, x −0.36), 3D offset −5.63.
      Done: Part-DB 577 and PS1 use it, +5V track moved to y 109.6; DRC 0 errors, 0 unconnected, parity clean.
- [x] **U3 (AMS1117) has no heat-spreading copper.** Tab on its pad and tracks only. 0.94 W at 0.55 A,
      estimated Tj ≈ 81 °C at 25 °C ambient (analyzer estimate, SOT-223 table value). Add a +3V3 pour
      around the tab, ideally with vias to a B.Cu island.
      _2026-10-04:_ +3V3 zone "U3 heat spreader" on F.Cu (priority 1, solid, 145–153 × 105.6–112.2, notched
      at C5 so its GND pad keeps two spokes). No B.Cu island: a signal bundle runs under the tab.
- [x] **3.3 V distribution on 0.25 mm tracks.** U3 → ESP32 pin 2: 50 mm, ≈ 67 mΩ (17.6 mm at 0.25 mm),
      about 35 mV at 0.5 A. U3 → W5500: 24 mm at 0.25 mm. Works, but widen to 0.5 mm; USB bench power
      already leaves little LDO headroom.
      _2026-10-04:_ U3 → ESP32 and U3 → L1/C14 0.5 mm throughout; U3 → W5500 pin 28 0.5 mm except the last
      5.7 mm around C15 (no room, C15 is local now). DRC 0 errors, 0 unconnected, parity clean.

- [x] **PS1 return pad on two thermal spokes.** Pad 2 (−VDC → GND, up to 1.8 A) touches the pour over
      1.0 mm of copper in total; the +VDC side has a 1.5 mm track. The Ag9900 datasheet asks for minimal
      thermal relief and power planes on both output pins to pull heat out of the module. Set that pad
      (and C2 pad 2, U3 pin 1) to a solid zone connection.
      _2026-10-04:_ DRC rule "Solid zone connection on power return pads" in `.kicad_dru` (PS1 pad 2,
      C2 pad 2, U3 pin 1); survives footprint updates. DRC after refill: 0 errors, 0 unconnected.
- [x] **Amplifier tracks are thin for 3 W.** OUTP/OUTN 0.25 mm over 25 / 17 mm (≈ 0.9 A at 10 K rise,
      which is the RMS current at 3 W into 4 Ω), GND pins 3/11/15 on single 0.2 mm tracks, last 3.3 mm of
      +5V at 0.25 mm. Widen the outputs and the supply to 0.4–0.5 mm. Uncritical with a small speaker.
      _2026-10-04:_ OUTP/OUTN 0.6 mm (fan-out at the pins narrower). +5V into pins 7/8 stays two parallel
      0.25 mm feeds. U6 exposed pad got 4 GND vias (0.45/0.25, 0.7 mm grid); the B.Cu +3V3 diagonal under
      it moved ~2 mm away. DRC 0 errors, 0 unconnected, parity clean.

## Minor

- [x] Four leftover track stubs from the block move (DRC `track_dangling`): `+5V` at (75.0, 108.3) and
      (95.98, 108.06), `+3V3` at (159.75, 107.35) and (160.61, 107.35). Delete. _Done 2026-10-04, DRC has
      no `track_dangling` left._
- [x] Amplifier supply: U6 is fed over 112 mm (42 mm at 0.5 mm on B.Cu, ≈ 70 mΩ) with only C27 10 µ 0402
      and C26 100 n nearby. Expect supply sag at high volume; add bulk (≥ 47 µ) at U6 on the next revision.
      *2026-10-04, for V0:* Part-DB 252 GRM31CR61A107MEA8L (100 µ 10 V X5R 1206, LCSC C883598, extended,
      54k stock), +5V/GND at U6. Place at (138.075, 77.4), rot 0: pad 1 (+5V) above the via at
      (136.535, 79.35), pad 2 GND into the pour plus a via. A 6.3 mm polymer like C2 does not fit there.
      Done as C21 at (138.2, 79.0), vertical, next to C26/C27; C27 GND pad tied by tracks, DRC rule
      `min_resolved_spokes 1` for it. DRC 0 errors, 0 unconnected, parity clean.
- [x] Adjacent relay circuits are 3.0 mm apart: fine on one phase, tight if two relays switch different
      phases (400 V). Document "all relays on the same phase" or widen.
      *2026-10-04:* widened. DRC rule `mains_between_relays` 4.0 mm; measured K1/K2 5.29, K2/K3 4.81,
      K3/K4 5.44 mm, tracks still 2.5 mm on all four layers. docs/12 relay row updated.
- [x] No ESD protection on USB or on the PIR / WS2812 terminals; no reset button (BOOT only).
      *Deferred to V1 (2026-10-04), noted in docs/12.*
- [x] W5500 has no auto-MDIX (datasheet 5.5.6). Fine on any switch with auto-MDIX; a direct link to an
      old 10/100 port needs a crossover cable.
      *Noted in docs/12 Ethernet row (2026-10-04).*
- [x] docs/12 "Checks run" still gives the G5LE pin numbers for the relay (1 COM, 3 NO, 4 NC, 2/5 coil).
      G2RL-1-E: coil 1/8, NC 2/7, COM 3/6, NO 4/5 (symbol pins A1/A2, 12, 11, 14). The relay row also
      says the 2.5 mm mains tracks are on both outer layers; they are on all four.
      *Fixed 2026-10-04, checked against the G2RL-1-E drawing; relay row already says all four layers.*
- [x] C3, C4, C27 (10 µ 0402 10 V) on 5 V keep ~3–4 µF under bias (accepted for V0; 0603 CL10A106KP8NNNC
      C19702 would fix it); AMS1117 output all-ceramic (D36 wants ESR).
      *Closed 2026-10-04:* C3/C4 sit in parallel with C2 (100 µ polymer), C27 is backed by C21 (100 µ). The
      AMS1117 datasheet in `datasheets/` (Slkor) only asks for output ESR ≤ 0.5 Ω, no minimum; C5 + C25 meet
      that. D36's "wants ESR" is the usual AMS1117 advice, not in this datasheet.
- [x] DMN601DWK VGS(th) max 2.5 V vs 3.3 V drive — marginal for the 80 mA coil at a worst-case part.
      *2026-10-04:* gate sees ≈ 3.0 V (1 K / 10 K). Q1/Q2 swapped to pin-compatible DMN2004DWK-7 (Part-DB 610,
      LCSC C156343, symbol `LRV:DMN2004DWK-7` extends DMN601DWK-7). Pin nets unchanged, DRC/parity clean.
- [x] Ag9905 min load 200 mA is borderline with Wi-Fi off (below it the module only gets audibly noisy);
      USB bench supply leaves little LDO headroom.
      *2026-10-04 estimate (datasheets):* 5 V idle with Wi-Fi off ≈ 175–250 mA, ≈ 200 typ (W5500 132, S3
      modem-sleep 240 MHz 33–81 + flash ≤ 10, LDO Iq 3–10, magjack LEDs 8–16, amp idle ≤ 3). Below 200 mA only
      the module whine; MPS is safe (≈ 20–25 mA at 48 V vs 10 mA). Plan: DNP bleeder R20, 100R 2512 1 W
      (UniOhm 25121WF1000T4E, LCSC C35218, new Part-DB entry) from +5V to GND, 50 mA / 0.25 W if fitted.
      Done: R20 (Part-DB 611) DNP at (92.86, 106.31) on the +5V trunk. Bring-up check: listen for module
      whine with Wi-Fi off, measure 5 V idle current; fit R20 if below 200 mA and audible.
- [x] Symbol pin types: MAX98357A PAD (unspecified → passive), W5500 INTn (input → output).
      *2026-10-04:* both are KiCad stock symbols (`Audio:MAX98357A` pin 17, `Interface_Ethernet:W5500` pin 36).
      Decision: leave the symbols, ERC-exclude the U6 pin 17 pin_to_pin warning; W5500 INT raises no warning.
      Exclusion set ("GND exposed pad").
- [ ] Stiching Vias
- [ ] Status LEDs
- [ ] Powering with both PoE and USB
- [ ] DRC silkscreen warnings: 139 overlaps, 112 over copper, 9 at the board edge. Cosmetic.

## Last step

- [ ] **Order files are stale.** `production/jlc_final/` was generated 2026-10-01; since then ~75 parts
      (magjack, W5500, ESP32, USB, audio, PoE front end) moved 9.065 mm left and routing changed. 127 of 209
      plated drill hits and 64 of 88 CPL rows no longer match the board. Regenerate gerbers, drill and
      `positions.csv` after the fixes above. `bom.csv` must be regenerated too: C21 (100 µ 1206,
      GRM31CR61A107MEA8L, LCSC C883598) was added and Q1/Q2 changed to DMN2004DWK-7 (C156343, was C156344) on
      2026-10-04. The other lines were correct on 2026-10-03.
- [ ] JLC placement preview: confirm polarity and pin 1 of C2, D9, D10, D11/D12, U1, U3, U4/U5, U6, J11,
      Y1, Q1/Q2 after uploading the new CPL.
