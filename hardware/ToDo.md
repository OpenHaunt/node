# OH_Node_V0_Prototype — pre-fab review 2026-10-03

Order: JLCPCB, all SMD assembled, THT by hand (docs/12 "Fabrication"). Relays switch **230 V mains**.
Evidence: `OH_Node_V0_Prototype/analysis/2026-10-03_1454-3/` (analyzer JSON), datasheets in `datasheets/`.
State checked: schematic of 2026-10-01 18:45, board of 2026-10-03 14:32. KiCad ERC/DRC clean (0 errors,
0 unconnected, parity clean, also with zones refilled); everything from the 2026-09-29 review is closed
except the items carried over under Minor.

## Blockers

- [ ] **Order files are stale.** `production/jlc_final/` was generated 2026-10-01; since then ~75 parts
      (magjack, W5500, ESP32, USB, audio, PoE front end) moved 9.065 mm left and routing changed. 127 of 209
      plated drill hits and 64 of 88 CPL rows no longer match the board. Regenerate gerbers, drill and
      `positions.csv` after the fixes below; `bom.csv` is still correct (36 lines, all LCSC numbers resolve
      to the right part and package, all in JLC stock — lowest: 74AHCT1G125W5-7 323, ECS crystal 477).
- [ ] **J1 audio jack slots too narrow.** Stock footprint `Jack_3.5mm_CUI_SJ1-3523N_Horizontal` drills
      0.4 × 1.4 mm slots; CUI's recommended layout is 1.5 × 0.8 mm, and JLC's minimum plated slot width is
      0.5 mm. Copy the footprint into LRV with 0.8 × 1.5 mm slots and larger pads (about 1.6 × 2.4 mm).

## Major

- [ ] **W5500 digital VDD (pin 28) has no local decoupling.** The only +3V3 caps near U1, C14 (1 µ) and C15
      (100 n), sit at the ferrite on the other side of the chip: 24–25 mm of 0.25 mm track to pin 28.
      Move C15 to pins 28/29 (between U1 and Y1), or add a 100 n there.
- [ ] **PS1 has the reflow land pattern but is hand-soldered.** Pads are 3.81 mm long and end at the module
      edge (Ag9900 datasheet Fig. 21: G 16.59, D 15.19, E 17.53). The hand-soldering pattern (Fig. 22) uses
      5.00 mm pads, G 17.88, D 15.85, E 18.19, about 1.2 mm visible outside the module. Change the
      footprint, or solder with paste and hot air.
- [ ] **U3 (AMS1117) has no heat-spreading copper.** Tab on its pad and tracks only. 0.94 W at 0.55 A,
      estimated Tj ≈ 81 °C at 25 °C ambient (analyzer estimate, SOT-223 table value). Add a +3V3 pour
      around the tab, ideally with vias to a B.Cu island.
- [ ] **3.3 V distribution on 0.25 mm tracks.** U3 → ESP32 pin 2: 50 mm, ≈ 67 mΩ (17.6 mm at 0.25 mm),
      about 35 mV at 0.5 A. U3 → W5500: 24 mm at 0.25 mm. Works, but widen to 0.5 mm; USB bench power
      already leaves little LDO headroom.

- [ ] **PS1 return pad on two thermal spokes.** Pad 2 (−VDC → GND, up to 1.8 A) touches the pour over
      1.0 mm of copper in total; the +VDC side has a 1.5 mm track. The Ag9900 datasheet asks for minimal
      thermal relief and power planes on both output pins to pull heat out of the module. Set that pad
      (and C2 pad 2, U3 pin 1) to a solid zone connection.
- [ ] **Amplifier tracks are thin for 3 W.** OUTP/OUTN 0.25 mm over 25 / 17 mm (≈ 0.9 A at 10 K rise,
      which is the RMS current at 3 W into 4 Ω), GND pins 3/11/15 on single 0.2 mm tracks, last 3.3 mm of
      +5V at 0.25 mm. Widen the outputs and the supply to 0.4–0.5 mm. Uncritical with a small speaker.

## Minor

- [ ] Four leftover track stubs from the block move (DRC `track_dangling`): `+5V` at (75.0, 108.3) and
      (95.98, 108.06), `+3V3` at (159.75, 107.35) and (160.61, 107.35). Delete.
- [ ] Amplifier supply: U6 is fed over 112 mm (42 mm at 0.5 mm on B.Cu, ≈ 70 mΩ) with only C27 10 µ 0402
      and C26 100 n nearby. Expect supply sag at high volume; add bulk (≥ 47 µ) at U6 on the next revision.
- [ ] Adjacent relay circuits are 3.0 mm apart: fine on one phase, tight if two relays switch different
      phases (400 V). Document "all relays on the same phase" or widen.
- [ ] No ESD protection on USB or on the PIR / WS2812 terminals; no reset button (BOOT only).
- [ ] W5500 has no auto-MDIX (datasheet 5.5.6). Fine on any switch with auto-MDIX; a direct link to an
      old 10/100 port needs a crossover cable.
- [ ] JLC placement preview: confirm polarity and pin 1 of C2, D9, D10, D11/D12, U1, U3, U4/U5, U6, J11,
      Y1, Q1/Q2 after uploading the new CPL.
- [ ] docs/12 "Checks run" still gives the G5LE pin numbers for the relay (1 COM, 3 NO, 4 NC, 2/5 coil).
      G2RL-1-E: coil 1/8, NC 2/7, COM 3/6, NO 4/5 (symbol pins A1/A2, 12, 11, 14). The relay row also
      says the 2.5 mm mains tracks are on both outer layers; they are on all four.
- [ ] C3, C4, C27 (10 µ 0402 10 V) on 5 V keep ~3–4 µF under bias (accepted for V0; 0603 CL10A106KP8NNNC
      C19702 would fix it); AMS1117 output all-ceramic (D36 wants ESR).
- [ ] DMN601DWK VGS(th) max 2.5 V vs 3.3 V drive — marginal for the 80 mA coil at a worst-case part.
- [ ] Ag9905 min load 200 mA is borderline with Wi-Fi off (below it the module only gets audibly noisy);
      USB bench supply leaves little LDO headroom.
- [ ] Symbol pin types: MAX98357A PAD (unspecified → passive), W5500 INTn (input → output).
- [ ] DRC silkscreen warnings: 139 overlaps, 112 over copper, 9 at the board edge. Cosmetic.
