# 12 — V0 Prototype

`hardware/OH_Node_V0_Prototype`, the single-board prototype of D37. Started 2026-09-09 as a copy of the
mainboard schematic; the schematic was completed the same evening. This is the as-built description.

## What is on the board

| Block | Parts | Notes |
|---|---|---|
| Ethernet | Würth 7499511420 magjack, W5500 | Pairs 1-2 and 3-6 only, gigabit pins open. LED1 green = activity, LED2 yellow = link. No auto-MDIX in the W5500 (datasheet 5.5.6): fine on any switch, a direct link to an old 10/100 port needs a crossover cable |
| PoE | 2 × MB10S-50MIL bridges (since 2026-10-01; were 4 × V6K100DUHM3/H), SMAJ58A, AG9912-MTB, C2 100 µ/25 V polymer (FP-025ME101M-SAR) + C3, C4 22 µ/25 V | **12 V** (since 2026-10-05; was AG9905-MTB, 5 V), 12 W to 70 °C, 9 W at 85 °C, 1.0 A. Class 0, min load 100 mA. Output caps per the datasheet: ≥ 100 µF / 25 V at the module. The MB10S is Silvertel's own Fig. 14 choice; ~0.2 W more loss than the Schottky pairs |
| 5 V | AP63205WU-7 (U7), L2 4.7 µH SWPA6045S4R7MT, C35 22 µ in, C36 + C37 22 µ out, C38 100 n bootstrap | 12 V → 5 V, 2 A, fixed output (FB to +5V), EN tied to VIN, spread spectrum. Values from table 3 of the AP63205 datasheet. Feeds every former +5V load |
| 3.3 V | AMS1117-3.3, C34 22 µ in, C5 10 µ + C25 22 µ out | C34 (CL21A226MAQNNNE, 0805, ≈ 10–14 µF at 5 V) on the +5V trunk about 3 mm from U3 pin 3; the datasheet recommends 10 µF at the input |
| Relays | 4 × Omron G2RL-1-E DC5, 2 × DMN2004DWK-7, 4 × 1N4148WS | 1 K gate series, 10 K pull-down on the GPIO side of the 1 K, so the gate sees the full 3.3 V. DMN2004DWK since 2026-10-04: VGS(th) ≤ 1.0 V, RDS(on) ≤ 0.7 Ω at 2.5 V; the DMN601DWK (VGS(th) up to 2.5 V, RDS(on) only specified at 5 V) was marginal for the 80 mA coil. Würth 691137710003 screw terminal per relay, pins 1–3: COM / NO / NC. **Max. 10 A per channel**: the 2.5 mm tracks run on all four layers; the two outer layers alone carry about 12 A at 20 K rise; relay and terminal could do 16 A. Relay circuits are at least 4 mm apart (DRC rule `mains_between_relays`, 4.8 mm measured), so neighboring relays may switch different phases. G2RL since 2026-09-30: 5 kV coil–contact, 8 mm / 8 mm, for 230 V outdoors; the G5LE was basic insulation only |
| PIR | 2 × Würth 691137710003: **12 V**, GND, signal | 12 V through a PPTC per terminal (F1/F2, 0805L020/30XR, 200 mA hold). Signal: 10 K pull-up to +3V3 at the terminal (R35/R37), 10 K series to the GPIO (R34/R36). Works with a contact to GND (NO or NC, polarity in firmware) and with an active 3.3 V output; a 12 V level on the signal pin injects only ~0.8 mA. Changed 2026-10-05 from 5 V, 1 K series, 100 K pull-down, which a contact to GND could not drive. Inputs read contacts and sinking (open-collector/NPN) outputs. A sensor that drives +12 V onto the signal pin and floats when idle always reads high: the 10 K pull-up holds the floating state high too |
| WS2812 | 2 × 74AHCT1G125 + 150 R, 10 K pull-down on each buffer input (R20, R49) | Würth 691137710002: data, GND. No 5 V on the terminal. The pull-downs hold the AHCT inputs low while GPIO17/18 float in reset, boot and download mode, so a strip shows no random pixels at power-up (added 2026-10-05) |
| Audio | MAX98357A, CUI SJ1-3523N, C21 100 µ 1206 bulk at U6 | Jack carries the bridged speaker output: tip OUTP, ring open, sleeve OUTN. Not line-out, and the sleeve is not ground; see "Line out" below. No WAGO terminal in parallel (D37 listed one); a speaker needs a 3.5 mm plug |
| Debug | USB-C GCT USB4215-03-A, 2 × 5K1 on CC, B5819W SL (D10) from VBUS to +5V | USB Serial/JTAG on the S3, no bridge chip. Board runs from USB on the bench |
| Expansion | J12, 1×8 pin header: 5 V, 3V3, GPIO2, GPIO38, GPIO39, SCL, SDA, GND | 4K7 I2C pull-ups to +3V3 on board. Not the 1×10 of D37. Sensors run from 3V3; a 5 V module with its own pull-ups to 5 V would put 5 V on GPIO21/47, which are not 5 V tolerant. 3V3 replaced GPIO1 on pin 2 on 2026-10-05 |
| Chassis | R1 1 M (0603) ∥ C1 1 n 2 kV (1206, FH 1206B102K202NT) between CHASSIS and GND, R5 0 R DNP | D30 bonding option, magjack shield on CHASSIS. Added 2026-09-29; missing before |

Removed against the mainboard: etherCON, discrete magnetics, Halo LEDs, all board-to-board connectors,
the console header. Two status LEDs (red, yellow) on GPIO35/36 since 2026-10-05. No eFuses, no metering, no descriptor EEPROM.

Deferred to V1 (decided 2026-10-04): ESD protection on USB and on the PIR / WS2812 terminals, and a reset
button. V0 is a bench prototype; USB Serial/JTAG resets the S3 and enters download mode without buttons,
BOOT is SW1, and power-cycling covers the rest. *V0.2 adds the reset button (SW2 on EN), see "V0.2 board
changes".*

## ESP32-S3 pin plan

Module `ESP32-S3-WROOM-1-N16R2`, quad PSRAM, so GPIO35–37 are free. GPIO0, 3, 45 and 46 are strapping
pins and stay off connectors.

| GPIO | Function |
|---:|---|
| 0 | boot button |
| 4, 5, 6, 7 | `RLY1`..`RLY4` |
| 9, 10, 11, 12, 13, 14 | W5500 SCLK, MOSI, MISO, SCSn, INTn, RSTn |
| 15, 16 | `PIR1`, `PIR2` |
| 17, 18 | `WS_OUT1`, `WS_OUT2` |
| 19, 20 | USB D−, D+ |
| 21, 47 | I2C SDA, SCL, on the header |
| 40, 41, 42 | I2S DIN, BCLK, LRCLK |
| 48 | `AMP_EN` (MAX98357A SD_MODE, high = on, left channel; V0.2: through R69/R70, (L+R)/2) |
| 2, 38, 39 | header GPIOs on J12 (GPIO2 is ADC1/touch, 38/39 digital only) |
| 43, 44 | UART0 TX/RX, on TP8/TP7 (since 2026-10-05) |
| 35 | `LED_R`: D2 red KT-0603R via R46 1K, error (high = on) |
| 36 | `LED_Y`: D1 yellow KT-0603Y via R45 1K, status (high = on) |
| 1, 8, 37 | spare, unconnected |

V0.2 (2026-10-17) uses the spares and GPIO39, and swaps GPIO18 and GPIO38:

| GPIO | Function |
|---:|---|
| 1, 8, 37, 3 | `LED_RLY1`..`LED_RLY4` |
| 45, 46 | `LED_PIR2` (D16, at J8), `LED_PIR1` (D17, at J10) |
| 39 | `XSMT` (PCM5102A mute), taken from J12 |
| 15, 16 | `PIR1` from J10, `PIR2` from J8 (inverted logic, see "V0.2 board changes") |
| 38 | `WS_OUT2` (U5), moved from GPIO18 for routing |
| 18 | header GPIO on J12 pin 5, in place of GPIO38 (ADC2_CH7, digital use only while WiFi runs) |

`WS_OUT1` stays on GPIO17. Firmware: the second WS2812 output is GPIO38 on V0.2 and GPIO18 on V0.1.

J12 in V0.2: 3V3, 5V, 12V, GPIO2, GPIO18, SCL, SDA, GND (pins 1–8).

## Firmware

OpenHaunt/firmware builds `oh-node-v0.1` and `oh-node-v0.2` (`scripts/build.sh <board>`). A V0 node
describes its on-board I/O as module type `0x0F01` "Node V0" (source `board`): relays on ports 0–3,
the sensor terminals on 4–5, and each strip as a head of its own (head 1 on ports 6–14, head 2 on
15–23, or no ports at all in `sacn` mode: see [14 — Pixel strips, and heads](14-pixels.md)),
identical on both revisions so the console makes one fixture type per head. The revision only changes the bindings: the sensor polarity
(a contact closed to GND reads *true* on both, so V0.1 inverts and V0.2 does not) and the second strip
pin. Ethernet (W5500, 20 MHz, the S3's own Ethernet MAC) comes first and Wi-Fi is the fallback. Details
and the reasons are in the firmware README, "The V0 prototype". None of it has run on a board yet.

## Power budget

**At 5 V** (unchanged loads): ESP32-S3 and W5500 through the LDO 1.8 W typical, 2.5 W peak. Four relays held
1.6 W. Amplifier 1 W typical, 3.5 W flat out. About 4.5 W typical, 7.5 W worst case.

**At 12 V** (since 2026-10-05): the 5 V loads through the AP63205 at roughly 90 %, so 5.0 W typical and
8.3 W worst case, plus the two sensor terminals, 12 V PIRs at 15–40 mA each, ≤ 1 W. About 5.5 W typical and
9.3 W worst case, against 12 W from the AG9912-MTB to 70 °C and 9 W at 85 °C. Worst case fits below 70 °C;
above that it is marginal, which V0 accepts. Min load 100 mA at 12 V (1.2 W), covered by the processor alone,
so the bleeder is gone (its designator R20 is now a WS2812 pull-down). Each terminal's PPTC holds 200 mA, so a terminal can draw up to 2.4 W before it
trips; the budget assumes sensors, not loads, on these terminals.

From USB on the bench: processor and network only, do not hold relays and drive the speaker at once. The
12 V rail is not powered from USB (see below), so 12 V sensors do not run on the bench.

**PoE and USB at the same time.** USB VBUS feeds +5V through D10 (B5819W SL), in parallel with the output of
the AP63205 buck (written for the AG9905 before 2026-10-05; the argument is the same with the buck in its
place). Normally the buck wins: 5.0 V against VBUS minus 0.2–0.3 V, so D10 stays off, nothing flows back into
the host, and the module's isolation keeps the PoE barrier intact with a laptop on USB. Worst case (buck at
the low end of its tolerance, VBUS 5.25 V) USB takes the load; PoE can then fall below its MPS current and be
dropped by the switch, and the node reboots when USB is unplugged. For a guaranteed PoE feed while debugging,
use a USB data-only adapter (VBUS blocker) or lift D10. Bring-up check: with both connected, measure which
source carries the load.

**USB only.** +5V sits at VBUS minus D10, about 4.7 V. The body diode of the AP63205's high-side FET then
charges +12V from the output side to roughly 4.3 V; that is above its 3.5 V UVLO, so the buck starts and runs
at full duty, and +12V settles near +5V. Harmless, but the sensor terminals and the module output see ~4.5 V
instead of 12 V, and the AG9912 output is back-driven, which its datasheet does not cover (the AG9905 had the
same issue). Bring-up check: with USB only, measure +12V and the current into PS1 +VDC.

## V0.2 power path (2026-10-17)

Replaces the PoE, 5 V and Debug rows above and the 12 V budget for the second revision. Goal: the whole
board runs from a USB power bank as well as from PoE. Decision in docs/10, D37 amendment of 2026-10-17.

| Block | Parts | Notes |
|---|---|---|
| PoE | AG9905-MTB (PS1, Part-DB 577), C2 100 µ/25 V + C3, C4 22 µ/25 V on `VPOE` | 5 V module, ADJ open (5.0 V). 9 W to 70 °C, 6 W at 85 °C, 1.8 A, min load 200 mA. Same symbol and footprint as the AG9912 |
| +5V mux | TPS2121RUXR (U8, Part-DB 628, `LRV:TPS2121RUX`), C51 1 µ on IN1, C39 1 µ on IN2, C52 1 µ on OUT | IN1 = `VPOE`, IN2 = `VBUS`. XREF mode (datasheet 10.4): U10 CJ431 (Part-DB 631, `LRV:CJ431`, pin 1 REF, 2 K, 3 A; the stock TL431DBZ has K and REF the other way round) as 2.5 V shunt reference on `PWRMUX_REF` = CP2, fed by R71 1K from +5V (~2.5 mA, needs ≥ 1 mA). PR1 divider R66 10 K / R68 12 K keeps PoE selected while VPOE > 2.5 V × 22/12 = 4.57 V (±1 % reference, 1 % resistors: about 4.45–4.69 V), independent of the USB voltage. Before +5V is up, CP2 is low and the internal 1.06 V reference also picks PoE. R67 47 K on ILIM: 65.2 / 47^0.861 = 2.37 A. C38 100 n on SS: 780 V/s, ~0.14 A inrush into the ~180 µF on +5V (C21, C27, C34, C35, plus C36/C37 through L2 and D21). 56 mΩ typ, 70 mΩ max at 25 °C: 0.05 W at 0.9 A, 0.15 W at 1.5 A. Reverse blocking both ways. OV1, OV2, ST to GND. Replaced the TPS22919 and two B5819W diodes, which would have dissipated 0.4–0.8 W each against a 500 mW rating |
| 3.3 V | AMS1117-3.3 (U3) directly on +5V | D13 and the `VIN_LDO` net are gone. ~1.6 V headroom on either source |
| 12 V | MT3608 (U7, Part-DB 624), L2 10 µH SWPA4030S100MT, D21 B5819W SL, C35 22 µ in, C36 + C37 22 µ out, R64 51 K / R65 2K7 | 0.6 V × (1 + 51/2.7) = 11.93 V. EN tied to IN, pin 6 NC. Feeds F1/F2 (PIR terminals) and `J12` pin 3 only. D21 carries only the sensor current. Layout per figure 3 of the MT3608 datasheet: C35–IN–GND loop as short as possible |

**Power budget on PoE.** The 5 V loads as listed under "At 5 V" above (4.5 W typical, 7.5 W worst), the mux
≤ 0.15 W, the 12 V sensors through the boost at about 85 %, ≤ 1.2 W. About 5 W typical and 8.9 W worst case
against 9 W from the AG9905 to 70 °C and 6 W at 85 °C. Worst case only just fits below 70 °C. Min load
200 mA at 5 V, covered by the processor.

**USB or power bank.** +5V sits at VBUS minus ~0.1 V. Everything runs, including the 12 V sensors. The mux
limits at about 2.4 A (2–3 A); the worst case is about 1.6 A. A power bank needs 5 V / 2 A or more; 5K1 on
CC1/CC2 makes it switch on. A 500 mA PC port browns out once relays and amplifier run.

**PoE and USB together.** PoE carries the load whenever it is valid, so its MPS current is always drawn and
nothing flows back into the host or the module. When PoE drops, the mux switches to USB within 5 µs (fast
switchover, CP2 above VREF); +5V dips by roughly 1 A × 5 µs / 130 µF ≈ 0.04 V below the 4.57 V threshold,
estimated from the datasheet's VOUT,MIN formula. With CP2 on GND, as first drawn, it was up to 100 µs and
~0.8 V, enough to brown out the S3.

**Short on a sensor cable.** The boost cannot disconnect its output, so the short draws from +5V through L2
and D21 (9 A surge rating) until the terminal's PPTC trips. +5V may dip long enough to reset the S3.

Bring-up checks: +12V at no load and with both sensors; +5V with PoE only, USB only and both; the +5V dip
when PoE is unplugged with USB connected, and whether the S3 survives it.

## V0.2 board changes (2026-10-17)

Everything else that changed for the second revision. Replaces the PIR, Audio and Debug rows of the first
table where they differ; the open list is `hardware/ToDo.md`.

| Block | Parts | Notes |
|---|---|---|
| PIR | per input: MMBT3904 (Q3/Q4, Part-DB 619), 1N4148WS (D18/D19), 10K pull-up (R35/R37), 10K base (R34/R36), 100K base–GND (R57/R59), 10K collector pull-up to +3V3 (R58/R60) | 12 V tolerant, nothing flows back into 3V3. Pull-up from +3V3 through R35 and the diode to the terminal; the diode blocks a 12 V level. Terminal → 10K → base, 100K to GND, collector to the GPIO. Contact to GND: transistor off, GPIO high. Open terminal or a 3.3–12 V level: GPIO low. **Logic is inverted against V0.1** (firmware). A series-R-plus-clamp cannot do both: a contact to GND reads ~1.65 V, or 12 V feeds mA into 3V3 through the clamp diode. 12 V through F1/F2 as before. J8 → Q3 → `PIR2` (GPIO16), J10 → Q4 → `PIR1` (GPIO15), swapped to match the board labels |
| Status LEDs | relays D3/D4/D14/D15 KT-0603R, PIRs D16/D17 KT-0805Y (Part-DB 620), each with 1K (R51–R56) | One GPIO per LED, low-side (GPIO → 1K → LED → GND), high = on. No LED ground switch; dark mode is firmware. GPIO3/45/46 are strapping pins: with low-side LEDs and no pull-ups they keep their default level. Relay LEDs sit at the same offset (12.8 mm) from each relay |
| Audio | PCM5102APWR (U9, Part-DB 618) line out on a Kycon STX-3000 (J1, Part-DB 617); MAX98357A (U6) on Würth 691137710002 (J13, Part-DB 585) | Both on the same I2S lines (GPIO40/41/42), so both get the same stream. PCM5102A in hardware mode: FMT, FLT, DEMP, SCK on GND (I2S, internal PLL from BCK), `XSMT` on GPIO39 with R61 10K to GND (muted until the firmware releases it). Output RC 470 Ω (R62/R63) / 2.2 nF (C46/C47) per channel. AVDD and CPVDD on `/AVDD` behind L3 BLM15AG601SN1D from +3V3, against WiFi bursts on the rail, with C40/C43 (CPVDD) and C41/C49 (AVDD); DVDD stays on +3V3 with C42/C50. Jack: tip = L, ring = R, sleeve = GND. Speaker: J13 pin 1 OUTP, pin 2 OUTN. SD_MODE through R69 47K from GPIO48, R70 10K to GND: ~0.53 V, the (L+R)/2 window of 0.355–0.65 V (datasheet Table 5, internal 100 kΩ ± 8 %), so the speaker plays a mono mix. GPIO48 low or floating = amplifier off. Neither chip has a volume control: volume is set by scaling the samples and applies to both outputs; XSMT and AMP_EN mute each output on its own |
| Buttons | SW1 BOOT on IO0, SW2 RST on EN, XKB TS-1187A (Part-DB 587) | Reset button no longer deferred |
| PoE thermal | 25 VIN− vias in the footprint, VIN− zone on all four layers inside the isolation ring | Fig. 12 of the Ag9900 datasheet. −VDC is the GND pour with vias at x 78. No separate +VDC plane |

**STX-3000 pinout.** Kycon's drawing gives pin 1 = sleeve (the long contact), pin 2 = the middle spring, pin 3
= the contact at the plug's end. CUI uses the same symbol on the SJ1-3523N and labels the end contact tip
and the middle spring ring; Kycon's STX-3501-3N, same symbol, puts the end-contact pin at the back of the
jack where the tip sits. So pin 2 = ring, pin 3 = tip. The footprint first had it the other way round and
was fixed on 2026-10-17. Pins 2 and 3 sit at the same depth, so the drawing's symbol is the only evidence;
confirm with a plug and a meter.

**GND stitching.** 162 GND vias added on 2026-10-17 with a script that checks clearance on all four layers,
keepouts and hole spacing: next to SMD GND pads more than 1 mm from a via, beside signal vias that had no
return via within 2 mm, and wherever the outer pours were more than 2.5 mm from one. Maps in
`hardware/OH_Node_V0_Prototype/analysis/2026-10-17_gnd/`. Five pads at U1 stay 1.1–1.4 mm from a via.

## Library additions made for V0

- `LRV:AG9900-MTB` symbol, footprint and STEP, family symbol for all Ag9900-MTB voltages
- `LRV:TerminalBlock_WAGO_2601-1103_1x03_P3.50mm_Horizontal`, stock footprint with WAGO's STEP, because
  KiCad 10.0.6 ships no WAGO 3D models
- Part-DB entries 577 to 581: AG9905-MTB, G5LE-1 DC5, 2601-1103, MAX98357AETE+T, SJ1-3523N
- V0.2: `LRV:TPS2121RUX` (no stock symbol). Pin 8 (second OUT) is hidden under pin 1; ST is passive so it
  can be tied to GND without an ERC error. Footprint `LRV:Texas_VQFN-HR-12_2x2.5mm_P0.5mm`, a copy of the stock
  one with a STEP built from TI's RUX0012A drawing, because KiCad 10.0.6 ships none for it
- V0.2: `LRV:CJ431`, a copy of the stock TL431DBZ with JSCJ's SOT-23 pin order (1 REF, 2 K, 3 A)
- V0.2: 25 VIN− thermal vias with a silk frame in both `LRV:AG9900-MTB*` footprints (Fig. 12); the TS-1187A
  3D model rotated onto its pads; STX-3000 T/R pad names fixed

## Checks run

Netlist parsed for single-node nets and non-Part-DB symbols, ERC clean apart from the MAX98357A thermal
pad warning. All 113 symbols came from Part-DB (2026-10-01; the four mounting holes are stock `Mechanical:MountingHole`). On 2026-10-05 the board has 142 components; the test points TP1–TP8 are stock `Connector:TestPoint` on purpose. The bridge topology was verified against `OH_Node_PWR_BT4`
(A1/A2 to the taps and K1/K2 to VIN+ on the high side, K1/K2 to the taps and A1/A2 to VIN− on the low side).
Relay pinout verified against the Omron G2RL-1-E drawing (datasheet p. 6, bottom view): coil 1/8, NC 2/7,
COM 3/6, NO 4/5, each contact on two pins. Symbol pins: A1/A2 coil, 12 NC, 11 COM, 14 NO.

## Before layout

- Domain 1 is the magjack center taps, the bridges, the TVS and the module input. Keep the 1500 V
  clearance to everything else.
- Keep-out under the module (Ag9905, now Ag9912) per Figure 17 of the datasheet.
- Check the Ag9900 3D model offset once in the viewer; it came from the CSE file.
- The board keeps the mainboard's 4-layer AISLER stackup. The magjack traces are the only controlled
  impedance. *Superseded by the JLC order below: the stackup and impedance geometry are JLC's.*

## Fabrication

Ordered from **JLCPCB** (decided 2026-09-29): the bare board plus assembly of every SMD part. JLC does
not fit the through-hole parts; they are soldered by hand.

What that changes:

- Every SMD part needs an LCSC number in the BOM. The LCSC numbers live in Part-DB as order details; the JLC
  BOM is generated by `analysis/make_bom.py` from the netlist and Part-DB (`production/bom_lcsc.csv`), because
  the fabrication plugin cannot read them from Part-DB. The plugin only supplies the CPL. PS1 is hand-soldered.
- Passives are JLC basic parts (Samsung, FH, Uniroyal) instead of the Yageo/Murata parts used elsewhere;
  Part-DB 589–609. Parts JLC does not stock go through global sourcing
  or are consigned.
- Stackup, impedance geometry and design rules are JLC's. The AISLER rules at the top of
  `OH_Node_V0_Prototype.kicad_dru` no longer apply to this board; the isolation and mains rules in the
  same file still do.

## Line out (found 2026-10-14, after ordering)

The MAX98357A is a speaker amplifier and V0 has no line output. `J1` carries the bridged class-D output
(tip `OUTP`, sleeve `OUTN`, both ~300 kHz PWM around 2.5 V, `GAIN_SLOT` open = 9 dB). A line input
grounds the sleeve: that shorts `OUTN`, trips the short-circuit protection or distorts, and feeds the PWM
carrier into the other device. **Never plug `J1` into a line input**; it is for a passive speaker only.

**V0 workaround, no board change:** a PCM5102A breakout (GY-PCM5102 type, own regulator and jack) on `J12`.
The S3 routes I2S to any GPIO and has two I2S peripherals, so the amplifier keeps GPIO40–42 and the DAC
uses the header:

| J12 | Breakout |
|---|---|
| 3V3 or 5 V | VIN |
| GND | GND |
| GPIO2, GPIO38, GPIO39 | BCK, LRCK, DIN |
| — | SCK tied to GND on the breakout (internal PLL) |

This uses all three header GPIOs. I2C stays on SDA/SCL. Untested.

**Next board:** line out and speaker, two chips. An I2S DAC (PCM5102A class: 3.3 V, ground-referenced
~2.1 Vrms, stereo, `XSMT` for mute) on a TRS jack with the sleeve on GND, and the MAX98357A on its own
speaker terminal (screw or WAGO, as D37 first planned), not on a jack. One chip doing both (TLV320DAC3100,
TAS2505, NAU88C22) was considered and rejected: about 1 W instead of 3.2 W, I2C setup before any sound,
and usually an extra 1.8 V rail. The DAC and its passives are not in Part-DB yet.

The line-out jack becomes a **Kycon STX-3000** (7.2 × 15.9 × 5.4 mm, €0.60, kinked pins, two locating
pegs) instead of the SJ1-3523N (12 mm wide), which was taken for V0 without a comparison. Footprint
`LRV:Jack_3.5mm_Kycon_STX-3000_Horizontal`, drawn from the Kycon drawing (rev D5): pads S/T/R for the
`AudioJack3` symbol, PCB edge marked 1.9 mm behind the front face so the bushing can pass through a panel.
3D model from SamacSys (Kycon offers none), placed by its own geometry: board plane at the 0.5 mm standoff,
not SamacSys's z offset, which sinks it 0.5 mm into the board. The model's kinked pins sit 0.15 mm off the
drawing's slot centers, inside the slots. Part-DB entry still to be created (DigiKey 2092-STX-3000-ND).
*Built in V0.2 as described ("V0.2 board changes"); Part-DB 617. The T and R pad names were swapped in the
first version of the footprint and fixed on 2026-10-17.*
