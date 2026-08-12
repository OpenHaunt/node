# 03 — Base Board

The base board is identical in every node. It provides power, network, compute, storage and the module
connector, and nothing else.

## Bill of materials

Prices are LCSC at 100 pieces unless noted, captured 2026-08-12. Stock figures move — treat them as an
indication of sourcing risk, not as fact.

| Function | Part | Source | ~Price | Notes |
|---|---|---|---|---|
| MCU module | **ESP32-WROOM-32UE-N4** | LCSC / DigiKey | ~$2.70 | U.FL variant, 4 MB flash, **no PSRAM** |
| Ethernet PHY | **RTL8201FI-VC-CG** | LCSC **C2763557** | $1.23 | QFN-32, **−40…+85 °C industrial** |
| Data flash | **W25Q128JVSIQ** | LCSC **C97521** | $1.95 | 16 MB, SOIC-8-208mil |
| PoE module | **Silvertel Ag9712-2BR** | Distributor | ~$10 | 12 V / 1.0 A / 12 W, 802.3af, 1500 V |
| Buck 5 V | **AP63205WU-7** | LCSC **C2071056** | ~$0.26 | 3.8–32 V in, 2 A, TSOT-26 |
| Buck 3.3 V | **AP63203WU-7** | LCSC **C780769** | ~$0.34 | 3.8–32 V in, 2 A, TSOT-26 |
| Antenna | **Taoglas GW.26.0151** | DigiKey | ~$6.65 | RP-SMA(M), 1.8 dBi, IP65, 30.3 mm |
| Antenna pigtail | **Taoglas CAB.774** | Taoglas / DigiKey | — | U.FL → RP-SMA(F) bulkhead, 100 mm |
| RJ45 magjack | **not yet selected** | — | — | **must be PoE-rated** — see below |
| 50 MHz oscillator | **not yet selected** | — | — | with enable pin — see below |
| Module connector | 2×8 pin header, 2.54 mm | — | ~$0.20 | see [04](04-module-interface.md) |

### Critical ordering notes

- **No `R` suffix on the ESP32 module.** `ESP32-WROOM-32UE-N4R2` and friends carry 2 MB of PSRAM, and that
  PSRAM occupies **GPIO16 and GPIO17** — the oscillator enable and the module UART TX. Same trap as the
  WROVER modules. The part number must end in `-N4`, `-N8` or `-N16` with no `R`.
- **The LCSC code for the 32UE-N4 needs confirming.** `C701345` is verified as the 32UE-**N8**. Note also that
  the UE variants carry far less LCSC stock than the E (PCB antenna) variants — around 930 versus 13,000 at
  the time of writing. If Wi-Fi is dropped (see [09](09-open-questions.md)), switching to
  `ESP32-WROOM-32E-N4` (LCSC **C701341**, ~$2.70, good stock) removes both the sourcing risk and the antenna.
- **Verify the RTL8201F footprint before layout.** LCSC lists the `-VB-CG` as `QFN-32-EP(5×5)` and the
  `-VC-CG` merely as `QFN-32`. `VB` and `VC` are different package/revision codes. Pull both mechanical
  drawings and compare. Getting this wrong is a respin, not a component swap.

### Why 4 MB of internal flash is enough

The external 16 MB chip holds the web UI assets, recorded sequences and logs. That leaves the internal flash
holding only code, which gives roughly **1.95 MB per OTA slot** after bootloader, partition table, NVS,
otadata and phy_init. A firmware with Wi-Fi, Ethernet, lwIP, an HTTP server, mDNS, Art-Net, sACN and the
module abstraction realistically lands at 1.1–1.4 MB, leaving 30–40 % headroom.

`-N8` remains a drop-in upgrade (identical footprint and pinout, ~$0.69 more at 100 pieces) if that headroom
turns out to be optimistic. **External flash does not extend code space** — memory-mapped execution only works
from the flash on SPI0.

## Pin allocation

Verified to close with one spare pin. `GPIO6–11` are consumed by the module's internal SPI flash and are
unavailable.

| GPIO | Function | Direction | Notes |
|---|---|---|---|
| 0 | RMII `REF_CLK` in | in | **Strapping pin.** Clock gated by GPIO16 — see below |
| 1 | `UART0_TX` | out | Programming / console header |
| 2 | `MOD_FAST0` | out | RMT-capable, e.g. WS2812. **Strapping — see below** |
| 3 | `UART0_RX` | in | Programming / console header |
| 4 | `MOD_UART_DE` | out | Direction control for RS-485-type modules |
| 5 | `FLASH_CS` | out | **Strapping** (must be high at boot) — 10 kΩ pull-up is also the correct idle state |
| 6–11 | internal SPI flash | — | Unavailable |
| 12 | *reserved, unused* | — | **MTDI strapping.** Must be low at boot or the chip picks 1.8 V flash and never boots. **Deliberately not routed to the module connector** |
| 13 | `FLASH_MOSI` | out | |
| 14 | `FLASH_SCLK` | out | |
| 15 | `MOD_nRESET` | out | **MTDO strapping.** 10 kΩ pull-down on base → module held in reset during boot |
| 16 | `OSC_EN` | out | 50 MHz oscillator enable |
| 17 | `MOD_UART_TX` | out | |
| 18 | RMII `MDIO` | i/o | Routable via GPIO matrix |
| 19 | RMII `TXD0` | out | **Fixed by hardware** |
| 21 | RMII `TX_EN` | out | **Fixed by hardware** |
| 22 | RMII `TXD1` | out | **Fixed by hardware** |
| 23 | RMII `MDC` | out | Routable via GPIO matrix |
| 25 | RMII `RXD0` | in | **Fixed by hardware** |
| 26 | RMII `RXD1` | in | **Fixed by hardware** |
| 27 | RMII `CRS_DV` | in | **Fixed by hardware** |
| 32 | `I2C_SDA` | i/o | Module bus + ID EEPROM |
| 33 | `I2C_SCL` | out | |
| 34 | `FLASH_MISO` | in | Input-only pin — ideal for MISO |
| 35 | `MOD_UART_RX` | in | Input-only pin — ideal for RX |
| 36 | `MOD_INT` | in | Input-only, open-drain from module, pull-up on base |
| 39 | *spare* | in | Input-only. Config button or module presence detect |

### Strapping pins — the three things that will bite you

**GPIO0 and the 50 MHz clock.** The RMII reference clock has to be present for the PHY, but GPIO0 is also the
boot mode strapping pin. If the oscillator is running at reset, the ESP32 may not enter download mode. This is
solved exactly as the WT32-ETH01 does it: the oscillator has an **enable pin driven by GPIO16**, held off
until strapping has been sampled. In the Arduino board definitions this pin is called `ETH_PHY_POWER`, which
obscures what it actually does.

**GPIO12 is not on the module connector.** MTDI selects the internal flash voltage at boot and must be low. A
module fitting a pull-up on it would render the base board unbootable — an unacceptable failure mode for a
connector that third parties may design against. It is left unrouted.

**GPIO2 and GPIO15 are on the connector, with rules.** The module interface contract
([04](04-module-interface.md)) forbids modules from fitting pull-ups on `MOD_FAST0` (GPIO2) or pull-downs that
fight the base board on `MOD_nRESET` (GPIO15). The base board fits a 10 kΩ pull-down on `MOD_nRESET` so the
module is held in reset through boot. **Side effect:** GPIO15 low at boot suppresses the ROM boot log on
UART0. For a product this is usually desirable; during bring-up, lift the resistor.

### Why the classic ESP32

It is the **only Espressif part with both Wi-Fi and an internal Ethernet MAC**. The ESP32-P4 has the MAC but
no radio; S3, C3, C5, C6, C61 and H4 have radios but no MAC and would need an external SPI Ethernet
controller. Full reasoning and the rejected alternatives are in [08 — Design Decisions](08-design-decisions.md).

## Ethernet subsystem

```
50 MHz OSC ──enable── GPIO16
      └── CLK ──┬── RTL8201FI CLKIN
                └── ESP32 GPIO0   (ETH_CLOCK_GPIO0_IN)

RMII data path (fixed):  GPIO19 TXD0 · GPIO22 TXD1 · GPIO21 TX_EN
                         GPIO25 RXD0 · GPIO26 RXD1 · GPIO27 CRS_DV
Management (matrix):     GPIO23 MDC  · GPIO18 MDIO
```

**Keep the external oscillator even though the RTL8201F can generate the 50 MHz clock itself.** If the PHY
sources the clock, it is running at power-on and the GPIO0 strapping problem returns, and you would have to
solve it by holding the PHY in reset instead. The external-oscillator topology works with any RMII PHY and is
the proven part of the WT32-ETH01 reference design — worth keeping even though the PHY itself changed.

ESP-IDF supports the PHY natively via `esp_eth_phy_new_rtl8201()`.

**Open:** the RJ45 magjack. It must be PoE-rated — centre taps brought out, DC bias tolerance (typically
350 mA per pair), 1500 V isolation. A common cheap magjack such as an HR911105A is **not** suitable and will
saturate. Bob Smith termination on the unused pairs must be DC-blocked.

## External flash

| Signal | GPIO |
|---|---|
| `SCLK` | 14 |
| `MOSI` | 13 |
| `MISO` | 34 (input-only) |
| `CS` | 5 (10 kΩ pull-up) |

Routed through the GPIO matrix at ~40 MHz, which is roughly 200× the bandwidth actually needed. The IOMUX
pins for HSPI would give 80 MHz but place MISO on GPIO12 — not worth the strapping risk for bandwidth nobody
needs.

**Two properties that make this chip worth having at all:**

1. **Writing to it does not stall the XIP cache.** The internal flash sits on SPI0/1, the same controller the
   CPU fetches code through; a 4 kB sector erase (30–50 ms) stalls execution. A chip on SPI2/SPI3 is an
   ordinary peripheral transaction. This is what makes recording data compatible with hard real-time
   effect timing.
2. **Wear is isolated from the boot medium.** If the data flash wears out after years of recording, the node
   still boots, still runs, still takes OTA updates — it just loses the recording feature.

**16 MB is the largest capacity that avoids 4-byte addressing.** 2²⁴ = 16,777,216 bytes exactly. Larger chips
require enabling 32-bit address mode, and getting the reset behaviour wrong produces silently shifted reads
rather than an error.

Endurance: 4096 sectors × 4 kB × 100,000 cycles ≈ **1.68 TB** of total writes. At raw DMX capture rates
(22.5 kB/s) that is ~2.4 years of continuous 24/7 recording before wear levelling inefficiency, so realistically
about a year of literal round-the-clock writing. For actual use it is effectively unlimited.

**Use LittleFS, not SPIFFS.** SPIFFS is deprecated in ESP-IDF, has no real wear levelling and degrades badly
with fragmentation. LittleFS is designed for NOR flash and is power-loss resilient — which matters for a
device whose power can be pulled at any moment by unplugging an Ethernet cable.

## Power stage

**Two independent bucks from 12 V, not cascaded.** Cascading 12→5→3.3 multiplies the efficiencies
(0.92 × 0.92 = 0.85) and couples the rails.

Both regulators use **Frequency Spread Spectrum** (±6 % switching frequency jitter). This is the reason for
choosing this family over a cheaper MP2315 or TPS562201: the board already carries the PoE module's switcher
and, on some modules, an isolated DC/DC. Adding two more fixed-tone 1.1 MHz switchers would be an avoidable
EMC problem.

## Layout constraints to carry into the schematic

- **The Ag9700 is a SIL module, 56 × 18 × 14 mm.** It defines the board outline. Silvertel's SMT families
  (Ag9900-MTB / Ag99xx-LPB, ~21.7 × 14.4 × 8.2 mm, auto-placeable, tape and reel) should be evaluated before
  committing — see [09 — Open Questions](09-open-questions.md).
- Silvertel specifies a **component keep-out area** around the PoE module.
- The antenna U.FL connector is rated for roughly **30 mating cycles** and tolerates no cable tension. Strain
  relieve the pigtail immediately behind the connector; the antenna is changed at the bulkhead, never at the
  module.
- The bulkhead must bond **electrically** to the metal enclosure — bare metal under the nut, not powder
  coating or anodising. The enclosure is the monopole's counterpoise, so a metal box is an advantage here,
  not a handicap.
- Keep the antenna feedthrough several centimetres from the RJ45 and, ideally, on a different face from the
  outgoing cables.
- Do not place the external flash next to any module's isolated DC/DC converter.
