# 12 — V0 Prototype

`hardware/OH_Node_V0_Prototype`, the single-board prototype of D37. Started 2026-09-09 as a copy of the
mainboard schematic; the schematic was completed the same evening. This is the as-built description.

## What is on the board

| Block | Parts | Notes |
|---|---|---|
| Ethernet | Würth 7499511420 magjack, W5500 | Pairs 1-2 and 3-6 only, gigabit pins open. LED1 green = activity, LED2 yellow = link |
| PoE | 4 × V6K100DUHM3/H as two bridges, SMAJ58A, AG9905-MTB, 100 µ polymer + 2 × 10 µ | 5 V, 9 W to 70 °C. Class 0, min load 200 mA |
| 3.3 V | AMS1117-3.3, 10 µ in, 22 µ out | |
| Relays | 4 × G5LE-1 DC5, 2 × DMN601DWK-7, 4 × 1N4148WS | 1 K gate series, 10 K pull-down. WAGO 2601-1103 per relay: NC / COM / NO |
| PIR | 2 × WAGO 2601-1103: 5 V, GND, signal | 1 K series, 100 K pull-down |
| WS2812 | 2 × 74AHCT1G125 + 150 R | WAGO 2601-1102: data, GND. No 5 V on the terminal |
| Audio | MAX98357A, CUI SJ1-3523N | Jack carries the bridged speaker output, tip+ring OUTP, sleeve OUTN. Not line-out |
| Debug | USB-C GCT USB4215-03-A, 2 × 5K1 on CC, 1N5819HW from VBUS to +5V | USB Serial/JTAG on the S3, no bridge chip. Board runs from USB on the bench |
| Expansion | 1×8 pin header: 5 V, four GPIOs, SCL, SDA, GND | 4K7 I2C pull-ups on board |
| Chassis | R1 1 M ∥ C1 1 n 2 kV between CHASSIS and GND, R5 0 R option | D30 bonding option, magjack shield on CHASSIS |

Removed against the mainboard: etherCON, discrete magnetics, Halo LEDs, all board-to-board connectors,
the console header, the status LED. No eFuses, no metering, no descriptor EEPROM.

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

## Power budget at 5 V

ESP32-S3 and W5500 through the LDO 1.8 W typical, 2.5 W peak. Four relays held 1.6 W. Amplifier 1 W
typical, 3.5 W flat out. About 4.5 W typical, 7.5 W worst case, against 9 W from the module and 6 W above
70 °C. From USB on the bench: processor and network only, do not hold relays and drive the speaker at once.

## Library additions made for V0

- `LRV:AG9900-MTB` symbol, footprint and STEP, family symbol for all Ag9900-MTB voltages
- `LRV:TerminalBlock_WAGO_2601-1103_1x03_P3.50mm_Horizontal`, stock footprint with WAGO's STEP, because
  KiCad 10.0.6 ships no WAGO 3D models
- Part-DB entries 577 to 581: AG9905-MTB, G5LE-1 DC5, 2601-1103, MAX98357AETE+T, SJ1-3523N

## Checks run

Netlist parsed for single-node nets and non-Part-DB symbols, ERC clean apart from the MAX98357A thermal
pad warning. All 114 symbols come from Part-DB. The bridge topology was verified against `OH_Node_PWR_BT4`
(A1/A2 to the taps and K1/K2 to VIN+ on the high side, K1/K2 to the taps and A1/A2 to VIN− on the low side).
Relay pinout verified against the Omron drawing: 1 COM, 3 NO, 4 NC, 2 and 5 coil.

## Before layout

- Domain 1 is the magjack centre taps, the bridges, the TVS and the module input. Keep the 1500 V
  clearance to everything else.
- Keep-out under the Ag9905 per Figure 17 of the datasheet.
- Check the Ag9900 3D model offset once in the viewer; it came from the CSE file.
- The board keeps the mainboard's 4-layer AISLER stackup. The magjack traces are the only controlled
  impedance.
