# 08 — Design Decisions

Why each choice was made, and what was rejected. This document exists so settled questions stay settled — if
you are about to reconsider one of these, read the entry first.

---

## D1 — Two-part architecture: base board + modules

**Decided.** One base board, many plug-on modules, rather than a family of complete devices.

**Why:** certification economics (the base stays a single SELV product with one assessment), board area (no
single board can carry DMX isolation, mains creepage and an LED power stage at once), development velocity (a
module is a fortnight, a product is not), and field repair (swap an €8 module, not a €60 device).

**Consequence:** the module interface in [04](04-module-interface.md) becomes the most safety-critical
document in the project, because it cannot change once boards are in the field.

---

## D2 — Classic ESP32, not S3 / P4 / C-series

**Decided:** ESP32-WROOM-32UE-N4 (classic ESP32, ESP32-D0WD-V3).

**Why:** it is the **only Espressif part with both Wi-Fi and an internal Ethernet MAC**.

| Considered | Wireless | Internal EMAC | Verdict |
|---|---|---|---|
| **ESP32 (classic)** | Wi-Fi 4, BT | **Yes, RMII** | **Selected** |
| ESP32-P4 | **None** | Yes, RMII | Would need a C6 companion for Wi-Fi — two chips, external PSRAM and flash, much higher dissipation |
| ESP32-S3 | Wi-Fi 4, BLE | No | Would need a W5500 — see D3 |
| ESP32-C5 | Dual-band Wi-Fi 6 | No | Dual-band is worthless on a PoE device; only 2 full UARTs |
| ESP32-C6 / C61 | Wi-Fi 6 | No | Too few peripherals |
| ESP32-H4 | **No Wi-Fi** (BLE + 802.15.4) | No | Neither network path — excluded |

*(For the record: "ESP32-S31" and "ESP32-E22" do not exist. E22 is an Ebyte LoRa module family.)*

**Cost of the decision:** 2016-era silicon, Wi-Fi 4, no native USB, and RAM permanently capped at 520 kB —
the PSRAM module variants are unusable because PSRAM occupies GPIO16/17.

**Note on how this was nearly got wrong:** the classic ESP32 was initially rejected on the grounds that UART0
is the console and only two UARTs would remain for three DMX ports. That reasoning was wrong — ESP32 UART
signals route through the GPIO matrix and UART0 can be remapped at runtime while the ROM bootloader still
forces GPIO1/3 in download mode. The point became moot when the port count dropped to one (D7), but the
reasoning is recorded so it is not repeated.

---

## D3 — Internal RMII MAC, not a W5500 over SPI

**Decided:** use the ESP32's internal Ethernet MAC.

**Why:** not throughput of our own traffic — one DMX universe is 210 kbit/s and a W5500 would manage 8–15
Mbit/s comfortably. The reason is **broadcast absorption**. An Art-Net console broadcasting 64 universes puts
13.4 Mbit/s at every node, which must be received and discarded. That is at or beyond the practical ceiling of
SPI Ethernet, and the failure mode is dropped packets and stuttering fades rather than a clean error.

ESP-IDF also drives the W5500 in MACRAW mode — its hardware TCP/IP stack is not used — so the "offload
engine" advantage is illusory.

**Also rejected with it:** the simpler layout (SPI instead of nine impedance-controlled RMII lines) and free
choice of MCU. Worth it.

---

## D4 — RTL8201FI-VC-CG, not LAN8720A

**Decided:** Realtek RTL8201FI-VC-CG, LCSC C2763557.

**Originally** the LAN8720A was selected, because both reference designs being copied (WT32-ETH01, Olimex
ESP32-POE) use it and Microchip publishes a complete datasheet. **Changed on availability** — LCSC stock fell
to double digits.

**The `I` suffix is not optional.** It is the industrial grade, −40…+85 °C. The commercial variant
(`RTL8201F-VB-CG`, $0.27 @100, excellent stock) is conventionally 0…+70 °C, and the thermal analysis in
[02](02-system-architecture.md) puts the PHY at a plausible 65–75 °C. The 96-cent premium at 100 pieces is
noise on a BOM containing a $10 PoE module.

**Costs of the change:**

- The reference designs no longer apply to the PHY. Strap options, PHY address and clock direction must be
  validated independently, and Realtek multiplexes straps onto LED and data pins — the classic source of
  "PHY not detected".
- **The footprint must be verified.** LCSC lists `-VB-CG` as `QFN-32-EP(5×5)` and `-VC-CG` only as `QFN-32`.
  `VB` and `VC` are different package codes. Compare both mechanical drawings before layout.

Both are natively supported by ESP-IDF (`esp_eth_phy_new_rtl8201()`).

---

## D5 — Keep the external 50 MHz oscillator with a GPIO16 enable

**Decided,** even though the RTL8201F can generate the RMII clock itself from a 25 MHz crystal.

**Why:** GPIO0 is both the RMII reference clock input and the boot strapping pin. A clock running at reset can
prevent the chip entering download mode. The WT32-ETH01 solves this with an **oscillator enable on GPIO16**,
held off until strapping has been sampled. If the PHY sourced the clock instead, the problem would return and
would have to be solved by holding the PHY in reset — a less proven path.

The oscillator topology works with any RMII PHY. It is the part of the reference design worth keeping even
though the PHY itself changed.

---

## D6 — CA-IS2092**VW**, not the plain W, and not the ADM2587E

**Decided:** Chipanalog CA-IS2092VW, LCSC C7422559.

**Against the ADM2587E** (the industry default): $1.44 versus $7.35 at 100 pieces, with *better* isolation
(3.75 kV rms with VDE/UL/CQC/TÜV approvals versus 2.5 kV rms) and a hand-solderable SOIC-16-WB. The ADI part's
one genuine advantage is **±15 kV IEC 61000-4-2** ESD versus Chipanalog's **±20 kV HBM** — HBM is a much
milder test and the two figures are not comparable. That gap is closed by the SM712 for $0.42, and the
combination ends up better protected than the bare ADM2587E.

**Why the `VW` and not the `W`:** the plain W has ratiometric input thresholds on `/RE`
(V_IH = 0.7 × VCC). At VCC = 5 V that requires 3.5 V, which a 3.3 V MCU cannot produce. The `VW` adds a
separate logic supply pin `VCCL` (2.375–5.5 V), so logic runs at 3.3 V while the isolated DC/DC runs from 5 V.
The ADM2587E offers no equivalent — with it you are forced to run VCC = 3.3 V.

**Also rejected:** the LGA-16 `CA-IS2092A` — marginally cheaper, but leadless (stencil required, X-ray
inspection) and with less creepage on a board that also carries an XLR connector.

---

## D7 — One DMX port, not three

**Decided.** Need a second universe? Hang a second node next to it.

**Why:** three bidirectional ports needed three hardware UARTs, three isolated transceivers (~300 mA of
isolated DC/DC alone), three isolation slots, and made the pin budget the binding constraint on MCU selection.
One port collapses all of that.

**Cost:** two switch ports and two PoE allocations instead of one. With classification programming (D13) that
is affordable.

---

## D8 — DMX is a module, not a base board function

**Decided,** despite the project having started as a DMX node.

**Why:** the base board stays small and cheap, and a node that only reads a reed switch does not carry an
isolated RS-485 transceiver and an XLR connector it will never use. Consistent with D1 — DMX is one actuator
among many.

**Consequence:** the module connector must carry a UART with direction control, which it does
(`MOD_UART_TX/RX/DE`).

---

## D9 — External data flash, and specifically 16 MB

**Decided:** W25Q128JVSIQ, 16 MB, LCSC C97521.

**Why external flash at all** — two reasons, neither of which is capacity:

1. **Writing to it does not stall the XIP cache.** The internal flash shares the SPI0/1 controller the CPU
   fetches code through; a 4 kB sector erase stalls execution for 30–50 ms, which collides with a DMX frame
   every 22.6 ms. A chip on SPI2/SPI3 is an ordinary peripheral transaction.
2. **Wear is isolated from the boot medium.** A worn-out data flash costs the recording feature, not the
   device.

**Why 16 MB and not more:** 2²⁴ = 16,777,216 bytes exactly — 16 MB is the largest capacity reachable with
standard 3-byte SPI addressing. Above it you must enable 4-byte address mode, and the W25Q256JV's
**non-volatile ADP bit** in status register 3 means a reset while the chip is in 4-byte mode, with a driver
assuming 3-byte, produces **silently shifted reads rather than an error**. Not worth it for capacity nobody
needs.

**Rejected:**

| Part | Size | $/100 | Why not |
|---|---|---|---|
| W25Q256JVFIQ | 32 MB | $3.52 | 4-byte addressing; SOIC-16-300mil (3× the area); only ~500 in stock |
| GD25Q256EYIGR | 32 MB | $2.81 | Better than the Winbond 32 MB (WSON-8, cheaper, more stock) but still 4-byte addressing |
| W25Q512JVFIQ | 64 MB | $12.98 | Price cliff — more than the rest of the logic section combined |

Capacity check: 16 MB gives ~45 s per free megabyte of raw DMX capture, and endurance of ~1.68 TB total
writes. With event-based rather than frame-based storage, effectively unlimited for this application.

---

## D10 — ESP32 module with 4 MB flash (`-N4`)

**Decided,** having initially recommended `-N8`.

**Why the change:** with the web UI assets on the external flash, the internal flash holds only code, giving
~1.95 MB per OTA slot. A firmware with Wi-Fi, Ethernet, lwIP, HTTP server, mDNS, Art-Net, sACN and the module
abstraction lands realistically at 1.1–1.4 MB — 30–40 % headroom.

`-N8` is footprint- and pin-identical (+$0.69 @100) and remains a drop-in if that turns out optimistic.
`-N16` buys nothing: firmware will not grow to 6 MB, and the real ceiling is RAM, not flash.

**Absolute rule: no `R` suffix.** `-N4R2` and siblings carry 2 MB PSRAM which occupies **GPIO16 and GPIO17** —
the oscillator enable and the module UART TX.

---

## D11 — 12 V PoE module, stepping down to 5 V and 3.3 V

**Decided:** Silvertel **Ag9712-2BR** (12 V / 1.0 A / 12 W), replacing the earlier Ag9705-2BR (5 V / 9 W).

**Why:** 12 V peripherals are needed — relay and contactor coils in particular. Deriving 12 V by boosting from
5 V at these currents would be poor engineering; taking the highest rail from the isolated module and stepping
down is correct.

Ag9700 family output options: 3.3 V / 1.8 A, 5 V / 1.8 A, **12 V / 1.0 A**, 24 V / 0.5 A (FL only). The
`-2BR` suffix brings the bridge rectifiers on-board, covering both Mode A and Mode B.

---

## D12 — Two independent bucks, not a cascade

**Decided:** 12 V → 5 V and 12 V → 3.3 V as separate converters.

**Why:** cascading 12→5→3.3 multiplies efficiencies (0.92 × 0.92 = 0.85) and couples the rails.

**Parts:** AP63205WU-7 (5 V) and AP63203WU-7 (3.3 V). Both 3.8–32 V in, 2 A, TSOT-26.

**Chosen for EMC, not price.** Both feature **Frequency Spread Spectrum** (±6 % jitter). The board already
carries the PoE module's switcher and, on DMX modules, an isolated DC/DC. A cheaper MP2315 or TPS562201 would
have added two more fixed-tone 1.1 MHz emitters. The 32 V input rating also gives comfortable margin over the
regulated 12 V rail.

---

## D13 — 802.3af, with classification programming strongly recommended

**Decided:** 802.3af (12.95 W at the PD).

**Sufficient** for the base board (~1.3 W) plus typical modules. Insufficient if a module needs a long WS2812
run or a large contactor — and moving to 802.3at later is a **base board respin**, not a component swap:
different module, different magjack rating, different thermal design.

**The classification point:** the Ag9700 presents a **Class 0 signature by default**, making the switch
reserve **15.4 W per port** for a device drawing 1.3–4 W. On a 370 W switch that is roughly 24 nodes instead
of 90. For a product whose scaling story is "hang another one next to it", the **-FL** variant with
programmable classification is worth the premium.

---

## D14 — SM712 always fitted; PPTC and series resistors as unpopulated footprints

**Decided.**

**SM712-02HTG** is always fitted: asymmetric −7 V / +12 V standoff matching the RS-485 common-mode range,
±30 kV IEC 61000-4-2, $0.42. At that price there is no case for leaving an XLR connector unprotected.

**PPTC and 10 Ω series resistors are footprints with 0 Ω fitted.** Surge (IEC 61000-4-5) is normally not
applied to signal ports; it only appears if the product is assessed under EN IEC 61000-6-2 and the >30 m line
rule bites. Deferring the decision costs two footprints and keeps the full driver swing in the default build.

**If fitted:** Bourns **MF-MSMF010-2** — the **60 V** rating is the whole point, because XLR-3 is also the
audio connector and 48 V phantom power will eventually be plugged in. The commonly recommended
`MF-PSMF010X-2` holds off only 15 V and is the wrong part. Series limiting is done by 10 Ω **pulse-withstanding
resistors** (ERJ-P06F10R0V), not by the PPTC — a PPTC takes ~1.5 s to trip and contributes only its 0.7 Ω
during an 8/20 µs event.

---

## D15 — Metal enclosure with an external antenna

**Decided:** Taoglas **GW.26.0151** (RP-SMA(M), 1.8 dBi, IP65) with a Taoglas **CAB.774** U.FL → RP-SMA(F)
bulkhead pigtail.

**Constraint:** the WROOM-32UE modular approval covers monopole antennas up to **2.33 dBi**. See
[07](07-compliance-and-safety.md) — this rules out most of the market, including the mechanically ideal
articulated Linx ANT-2.4-CW-HWR at 3.2 dBi.

**The metal enclosure is an advantage here**, not a handicap: a whip is a monopole and needs a counterpoise.
The bulkhead bonds to the enclosure, which becomes the ground plane. It must bond to **bare metal** — not
through powder coating or anodising.

**Open:** whether Wi-Fi is needed at all. The device is PoE-powered and therefore always has an Ethernet
cable. Dropping Wi-Fi removes the antenna, the pigtail, the bulkhead, the enclosure penetration, the strain
relief and the antenna compliance argument, and allows the better-stocked `ESP32-WROOM-32E-N4`. See
[09](09-open-questions.md).
