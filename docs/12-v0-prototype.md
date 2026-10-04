# 12 — V0 Prototype

`hardware/OH_Node_V0_Prototype`, the single-board prototype of D37. Started 2026-09-09 as a copy of the
mainboard schematic; the schematic was completed the same evening. This is the as-built description.

## What is on the board

| Block | Parts | Notes |
|---|---|---|
| Ethernet | Würth 7499511420 magjack, W5500 | Pairs 1-2 and 3-6 only, gigabit pins open. LED1 green = activity, LED2 yellow = link. No auto-MDIX in the W5500 (datasheet 5.5.6): fine on any switch, a direct link to an old 10/100 port needs a crossover cable |
| PoE | 2 × MB10S-50MIL bridges (since 2026-10-01; were 4 × V6K100DUHM3/H), SMAJ58A, AG9905-MTB, 100 µ polymer (Panasonic 16SVPC100M) + 2 × 10 µ | 5 V, 9 W to 70 °C. Class 0, min load 200 mA. The MB10S is Silvertel's own Fig. 14 choice; ~0.2 W more loss than the Schottky pairs |
| 3.3 V | AMS1117-3.3, 10 µ in, 22 µ out | |
| Relays | 4 × Omron G2RL-1-E DC5, 2 × DMN2004DWK-7, 4 × 1N4148WS | 1 K gate series, 10 K pull-down (≈ 3.0 V at the gate). DMN2004DWK since 2026-10-04: VGS(th) ≤ 1.0 V, RDS(on) ≤ 0.7 Ω at 2.5 V; the DMN601DWK (VGS(th) up to 2.5 V, RDS(on) only specified at 5 V) was marginal for the 80 mA coil. Würth 691137710003 screw terminal per relay, pins 1–3: COM / NO / NC. **Max. 10 A per channel**: the 2.5 mm tracks run on all four layers; the two outer layers alone carry about 12 A at 20 K rise; relay and terminal could do 16 A. Relay circuits are at least 4 mm apart (DRC rule `mains_between_relays`, 4.8 mm measured), so neighbouring relays may switch different phases. G2RL since 2026-09-30: 5 kV coil–contact, 8 mm / 8 mm, for 230 V outdoors; the G5LE was basic insulation only |
| PIR | 2 × Würth 691137710003: 5 V, GND, signal | 1 K series, 100 K pull-down |
| WS2812 | 2 × 74AHCT1G125 + 150 R | Würth 691137710002: data, GND. No 5 V on the terminal |
| Audio | MAX98357A, CUI SJ1-3523N, C21 100 µ 1206 bulk at U6 | Jack carries the bridged speaker output: tip OUTP, ring open, sleeve OUTN. Not line-out, and the sleeve is not ground |
| Debug | USB-C GCT USB4215-03-A, 2 × 5K1 on CC, 1N5819HW from VBUS to +5V | USB Serial/JTAG on the S3, no bridge chip. Board runs from USB on the bench |
| Expansion | 1×8 pin header: 5 V, four GPIOs, SCL, SDA, GND | 4K7 I2C pull-ups on board |
| Chassis | R1 1 M (0603) ∥ C1 1 n 2 kV (1206, FH 1206B102K202NT) between CHASSIS and GND, R5 0 R DNP | D30 bonding option, magjack shield on CHASSIS. Added 2026-09-29; missing before |

Removed against the mainboard: etherCON, discrete magnetics, Halo LEDs, all board-to-board connectors,
the console header. Two status LEDs (red, yellow) on GPIO35/36 since 2026-10-05. No eFuses, no metering, no descriptor EEPROM.

Deferred to V1 (decided 2026-10-04): ESD protection on USB and on the PIR / WS2812 terminals, and a reset
button. V0 is a bench prototype; USB Serial/JTAG resets the S3 and enters download mode without buttons,
BOOT is SW1, and power-cycling covers the rest.

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
| 48 | `AMP_EN` (MAX98357A SD_MODE, high = on, left channel) |
| 38, 39 + two more | header GPIOs. Use 1 and 2 for the other two, not 45/46 |
| 43, 44 | UART0, unused, no header |
| 35 | `LED_R`: D2 red KT-0603R via R46 1K, error (high = on) |
| 36 | `LED_Y`: D1 yellow KT-0603Y via R45 1K, status (high = on) |
| 8, 37 | spare, unconnected |

## Power budget at 5 V

ESP32-S3 and W5500 through the LDO 1.8 W typical, 2.5 W peak. Four relays held 1.6 W. Amplifier 1 W
typical, 3.5 W flat out. About 4.5 W typical, 7.5 W worst case, against 9 W from the module and 6 W above
70 °C. From USB on the bench: processor and network only, do not hold relays and drive the speaker at once.

**PoE and USB at the same time.** USB VBUS feeds +5V through D10 (1N5819HW), in parallel with the AG9905
output. Normally the module wins: 5.0 V against VBUS minus 0.2–0.3 V, so D10 stays off, nothing flows back into
the host, and the module's isolation keeps the PoE barrier intact with a laptop on USB. Worst case (module
4.75 V, VBUS 5.25 V) USB takes the load; PoE can then fall below its MPS current and be dropped by the switch,
and the node reboots when USB is unplugged. With USB only, VBUS back-drives the unpowered module output,
which the AG9905 datasheet does not cover. For a guaranteed PoE feed while debugging, use a USB data-only
adapter (VBUS blocker) or lift D10. Bring-up check: with both connected, measure which source carries the
load; with USB only, measure the current into PS1 +VDC.

## Library additions made for V0

- `LRV:AG9900-MTB` symbol, footprint and STEP, family symbol for all Ag9900-MTB voltages
- `LRV:TerminalBlock_WAGO_2601-1103_1x03_P3.50mm_Horizontal`, stock footprint with WAGO's STEP, because
  KiCad 10.0.6 ships no WAGO 3D models
- Part-DB entries 577 to 581: AG9905-MTB, G5LE-1 DC5, 2601-1103, MAX98357AETE+T, SJ1-3523N

## Checks run

Netlist parsed for single-node nets and non-Part-DB symbols, ERC clean apart from the MAX98357A thermal
pad warning. All 113 symbols come from Part-DB (2026-10-01; the four mounting holes are stock `Mechanical:MountingHole`). The bridge topology was verified against `OH_Node_PWR_BT4`
(A1/A2 to the taps and K1/K2 to VIN+ on the high side, K1/K2 to the taps and A1/A2 to VIN− on the low side).
Relay pinout verified against the Omron G2RL-1-E drawing (datasheet p. 6, bottom view): coil 1/8, NC 2/7,
COM 3/6, NO 4/5, each contact on two pins. Symbol pins: A1/A2 coil, 12 NC, 11 COM, 14 NO.

## Before layout

- Domain 1 is the magjack centre taps, the bridges, the TVS and the module input. Keep the 1500 V
  clearance to everything else.
- Keep-out under the Ag9905 per Figure 17 of the datasheet.
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
