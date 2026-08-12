# 05 — Module Catalog

Planned modules. Only the DMX module has a completed component selection; the rest are specified to the level
of "we know what it is and what it costs us".

| ID | Module | Bus lines used | Status |
|---:|---|---|---|
| `0x0001` | DMX512 / RDM output | UART TX/RX/DE | **Components selected** |
| `0x0002` | Digital input (8×) | I²C, INT | Concept |
| `0x0003` | Addressable LED (WS2812) | FAST0 | Concept |
| `0x0004` | Mains relay, 3~ + N | I²C or GPIO, remote | Concept — see safety notes |
| `0x0005` | OLED display | I²C | Concept |
| `0x0006` | Dry contact output (4×) | I²C | Concept |
| `0x0007` | Environmental sensor | I²C | Concept |

---

## 0x0001 — DMX512 / RDM output

The module the project started as. One isolated DMX universe on an XLR connector.

### Bill of materials

| Function | Part | Source | ~Price |
|---|---|---|---|
| Isolated transceiver | **Chipanalog CA-IS2092VW** | LCSC **C7422559** | $1.44 |
| Bus protection | **Littelfuse SM712-02HTG** | LCSC **C172881** | $0.42 |
| Overcurrent (optional) | Bourns **MF-MSMF010-2** | LCSC **C12430** | — |
| Surge limiting (optional) | 2× Panasonic **ERJ-P06F10R0V** | DigiKey | — |
| ID EEPROM | 24AA02UID | — | ~$0.15 |
| Connector | XLR, 3- or 5-pin — **not yet decided** | — | — |

### Why the CA-IS2092VW

An isolated RS-485 transceiver with an **integrated isolated DC/DC converter** — no separate isolated supply
needed. Selected over the industry-standard ADI ADM2587E on price: roughly $1.44 versus $7.35 at 100 pieces
for functionally equivalent parts.

| | CA-IS2092VW | ADM2587E |
|---|---|---|
| Isolation | 3.75 kV rms (60 s), VDE / UL 1577 / CQC / TÜV | 2.5 kV rms |
| Data rate | 500 kbps, slew-rate limited | 500 kbps, slew-rate limited |
| ESD on bus pins | ±20 kV **HBM** | ±15 kV **IEC 61000-4-2** |
| Separate logic supply | **Yes — VCCL, 2.375–5.5 V** | No |
| Package | SOIC-16-WB, hand-solderable | SOIC-20W |
| Price @100 | **$1.44** | $7.35 |

The **VW variant is essential**, not a preference. The plain `W` has ratiometric input thresholds on `/RE`
(V_IH = 0.7 × VCC). At VCC = 5 V that demands 3.5 V, which a 3.3 V MCU cannot deliver. The `VW` adds a
separate logic supply pin `VCCL`, so:

- `VCCL` = 3.3 V → all logic levels match the ESP32
- `VCC` = 5 V → feeds the isolated DC/DC
- `SEL` selects the isolated-side supply (3.3 V or 5 V)

Note the caveat: with VCC = 3.3 V, `SEL` must float or tie to GND_B, forcing VISO = 3.3 V. Only VCC = 5 V
gives a free choice.

### Why DMX works fine at 3.3 V on the bus side

Worth recording because it is counter-intuitive. RS-485 is **differential**: the receiver decides on the A−B
difference, with a threshold of ±200 mV. The standard requires the driver to deliver ≥1.5 V differential into
54 Ω. Both the CA-IS2092VW and the ADM2587E regulate their isolated side to ~3.3 V regardless of input supply,
and both meet the 1.5 V minimum. A higher supply buys nothing on the bus.

### Protection

```
XLR pin 2/3 ──▶ [PPTC] ──▶ SM712 (to GND_B) ──▶ [10 Ω] ──▶ A/B on CA-IS2092VW
```

- **SM712-02HTG**: asymmetric −7 V / +12 V standoff matching the RS-485 common-mode range, **±30 kV
  IEC 61000-4-2** contact and air, 19 A at 8/20 µs, 600 W, SOT-23. Always fitted.
- **Its ground pin goes to GND_B** — the isolated cable-side ground, the same reference as XLR pin 1. Tying it
  to chassis or logic ground shorts out the 3.75 kV barrier the module exists to provide.
- **PPTC and series resistors are footprint-only, 0 Ω fitted by default.** They are needed only if the product
  has to demonstrate surge immunity (IEC 61000-4-5), which is not normally required at a signal port unless
  the assessment uses the industrial generic standard and the >30 m rule applies. See
  [07 — Compliance & Safety](07-compliance-and-safety.md).
- If they are fitted: **MF-MSMF010-2** (0.1 A hold, **60 V**, 0.7 Ω, 1812) — the 60 V rating is the point,
  because XLR-3 is also the audio connector and someone will eventually plug in 48 V phantom power. The common
  `MF-PSMF010X-2` can only hold off 15 V and is the wrong part here. Plus 2× **ERJ-P06F10R0V**, 10 Ω 1 %
  pulse-withstanding, for the actual surge current limiting — a PPTC takes ~1.5 s to trip and does nothing
  during an 8/20 µs event except present its 0.7 Ω.
- Signal cost with both fitted: 2 × 10.7 Ω in series with 54 Ω leaves ~72 % of the driver swing at the
  connector, ~1.1 V against a 200 mV receiver threshold. Formally outside the RS-485 driver spec at the
  connector; DMX512-A does not test this.

### Free capabilities

The transceiver is half-duplex with `DE` control. Keeping the `RX` and `DE` lines routed costs nothing and
leaves **RDM** and **DMX input** open as pure firmware questions. Do not optimise these away in hardware.

### Open

XLR 3-pin or 5-pin. 5-pin is the DMX512-A standard; 3-pin is what the entire haunt and small-venue market
actually uses. This affects the enclosure outline and should be decided before mechanical design.

---

## 0x0002 — Digital input (8×)

Buttons, reed switches, PIR sensors, door contacts, prop triggers. The most common module in an escape room.

- 8 opto-isolated inputs via an I²C expander (MCP23017 or PCF8574 family)
- `MOD_INT` asserted on any change — no polling, and the firmware can timestamp the edge
- Screw terminals or a pluggable terminal block
- 12 V available on the terminals for powering PIRs
- **Opto-isolation matters here**: input wiring in a haunt runs long distances alongside mains and DMX, and
  arrives with whatever the environment induced onto it

**Latency is the design driver.** A scare that fires late is not a scare. The path from contact closure to
network event must be interrupt-driven end to end.

## 0x0003 — Addressable LED (WS2812)

- Level shifter 3.3 V → 5 V on `MOD_FAST0`, driven by the ESP32 RMT peripheral
- 5 V power for the strip — **this is the constraint**, not the data line
- **Power budget is the hard limit.** At ~60 mA per RGB pixel at full white, the ~10.4 W total node budget is
  roughly 55 pixels at full brightness. A realistic effect run needs external 5 V injection, and the module
  should provide a terminal for it plus a jumper to select bus-powered or externally-powered operation
- Series resistor on the data line, capacitor at the strip end, and the usual first-pixel-sacrifice practice

## 0x0004 — Mains relay, 3 phases + neutral

**Read [07 — Compliance & Safety](07-compliance-and-safety.md) before designing this module.**

The requirement is to interrupt a three-phase 230/400 V supply on all three lines and neutral.

**Use one four-pole device with a single coil — not four individual relays.** The neutral pole must not open
before the line conductors and must close no later than them. With four independent relays that sequencing
depends on coil tolerances, temperature and ageing; you would have a safety-relevant property determined by
component spread. A four-pole contactor or four-pole relay has manufacturer-defined, tested pole sequencing.

Candidates: four-pole installation contactors with 12 V DC coils (Finder 22.44, ABB ESB, Eaton Z-SCH).
Coil power ~1.5–2.5 W → 125–210 mA at 12 V, which fits the budget — but is also **2 W of continuous heat**
whenever energised.

Verify before selecting:

- **Pole-to-pole insulation rated for 400 V.** Many multi-pole relays specify contact-to-contact withstand
  only within one phase.
- **Creepage and clearance** per IEC 60664-1 for 400 V in overvoltage category III. Pull the values from the
  tables; do not estimate them.
- **Utilisation category.** AC-1 for resistive loads, AC-3 for motors. With switch-mode supplies as the load —
  which is most haunt equipment — inrush is the real sizing criterion, not steady-state current.
- **De-energised state.** Should the load drop out when the node loses PoE? Safer, but "the lights go out
  because a switch rebooted" is usually the worse outcome on site. This is a product decision.

**Bistable (latching) contactors** eliminate the 2 W of standing coil dissipation, at the cost of a second
control line and a much harder question about what state the device should assume after a power failure.

## 0x0005 — OLED display

I²C SSD1306 or SH1106, 128×64. Countdown timers, hint display, prop instrumentation, and — usefully — a local
diagnostic readout showing IP address and module status without a laptop.

## 0x0006 — Dry contact output (4×)

Four low-voltage relay or solid-state outputs for triggering third-party equipment: audio players, smoke
machines with contact-closure inputs, existing show controllers. The glue module that lets OpenHaunt drive
things it does not otherwise speak to.

## 0x0007 — Environmental sensor

Temperature, humidity, air quality via I²C. Less about effects, more about operations: fog fluid behaviour
depends on humidity, and an escape room's HVAC failing mid-season is worth knowing about before the guests
tell you.
