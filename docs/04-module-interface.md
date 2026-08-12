# 04 — Module Interface

This is the contract between the base board and everything that plugs into it. It is the most important
document in the repository: once boards exist in the field, this interface cannot change without splitting the
ecosystem.

## Design goals

1. **A module must never be able to prevent the base board from booting.** Third parties may design modules;
   the base board must be robust against a badly designed one.
2. **The base board must be able to identify the module without prior knowledge.** This is what makes
   plug-and-play discovery possible.
3. **Timing-critical signalling must be possible.** WS2812 needs a precisely timed 800 kHz line; that cannot
   go through an I²C expander.
4. **Any additional isolation is the module's job.** The base board's connector is always SELV.

## Connector pinout

**2×8 pin header, 2.54 mm pitch.** Odd pins in the left column, even pins in the right.

| Pin | Signal | Dir | Description |
|---:|---|:---:|---|
| 1 | `+12V` | → | Main module supply |
| 2 | `+12V` | → | Second pin for current capacity |
| 3 | `GND` | — | |
| 4 | `GND` | — | |
| 5 | `+5V` | → | From AP63205 |
| 6 | `+3V3` | → | Logic reference — **all bus signals are 3.3 V** |
| 7 | `GND` | — | |
| 8 | `I2C_SDA` | ↔ | Pull-ups (4.7 kΩ) on the base board |
| 9 | `I2C_SCL` | → | |
| 10 | `MOD_INT` | ← | Open drain, active low, pull-up on base. Module signals an event |
| 11 | `MOD_UART_TX` | → | Base → module |
| 12 | `MOD_UART_RX` | ← | Module → base |
| 13 | `MOD_UART_DE` | → | Driver enable for RS-485-type modules |
| 14 | `MOD_FAST0` | → | RMT-capable timed output (WS2812 etc.) |
| 15 | `GND` | — | |
| 16 | `MOD_nRESET` | → | Active low. Base holds module in reset through boot |

Keying: pin 1 marked on both boards, plus a mechanical key (see *Mechanics* below). **A reversed module must
not destroy anything** — consider a series element or reverse-polarity protection on the module's 12 V input.

## Power contract

| Rail | Available to modules | Notes |
|---|---|---|
| `+12V` | **~850 mA** | Shared budget — see below |
| `+5V` | ~500 mA | Drawn from the same 12 W total |
| `+3V3` | ~300 mA | Drawn from the same 12 W total |

**These are not independent allowances.** All three come out of the ~10.4 W remaining after the base board.
A module drawing 500 mA at 5 V has consumed roughly a quarter of the total budget, not "half of a separate 5 V
rail".

A module's descriptor declares its worst-case draw. Firmware reads this at boot and can refuse to enable a
module whose declared draw exceeds the budget, rather than browning out the node mid-show.

## Signal rules for module designers

These exist because of ESP32 strapping behaviour and are not negotiable:

- **Do not fit a pull-up on `MOD_FAST0` (pin 14).** It maps to GPIO2. A pull-up prevents the base board from
  entering download mode. A pull-*down* is fine.
- **Do not fight `MOD_nRESET` (pin 16).** It maps to GPIO15 and carries a 10 kΩ pull-down on the base board.
  Treat it as an input to your module only.
- **All bus signals are 3.3 V logic.** If your module runs at 5 V — WS2812 data, for example — level shift on
  the module.
- **`MOD_INT` is open drain.** Never drive it high. Multiple sources may share it in future revisions.
- **Do not assume the module is powered before `MOD_nRESET` is released.** Rails may be live while firmware
  is still deciding whether to enable you.

## Module identification

Every module carries an **I²C EEPROM at address `0x50`** holding a descriptor. This is what turns "a board is
plugged in" into "this node is a DMX output with serial 0x1A2B and needs 180 mA".

**Recommended part:** `24AA02UID` — 2 kbit, with a factory-programmed globally unique 32-bit serial in the
upper page. That gives every module a traceable identity with no programming step in production. A plain
`AT24C02` works if the serial is written at test.

### Descriptor layout (draft)

Little-endian, offset from EEPROM address 0x00.

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0x00 | 4 | `magic` | ASCII `"OHM1"` — OpenHaunt Module, format version 1 |
| 0x04 | 1 | `descriptor_version` | Format revision, starts at 1 |
| 0x05 | 2 | `module_type` | Registered type ID — see [05](05-module-catalog.md) |
| 0x07 | 1 | `hw_revision` | Board revision |
| 0x08 | 2 | `power_mA_12v` | Declared worst-case draw at 12 V |
| 0x0A | 2 | `power_mA_5v` | Declared worst-case draw at 5 V |
| 0x0C | 2 | `power_mA_3v3` | Declared worst-case draw at 3.3 V |
| 0x0E | 2 | `capability_flags` | Bitfield — which bus lines the module uses |
| 0x10 | 16 | `name` | Human-readable, null-padded ASCII |
| 0x20 | 16 | `vendor` | Null-padded ASCII |
| 0x30 | 2 | `crc16` | Over 0x00–0x2F |
| 0xF8 | 8 | *(24AA02UID)* | Factory-programmed unique serial, read-only |

### Capability flags

| Bit | Meaning |
|---:|---|
| 0 | Uses `MOD_UART_TX` |
| 1 | Uses `MOD_UART_RX` |
| 2 | Uses `MOD_UART_DE` |
| 3 | Uses `MOD_FAST0` |
| 4 | Uses `MOD_INT` |
| 5 | Has additional I²C devices beyond the EEPROM |
| 6 | Module contains a mains-voltage section |
| 7 | Module provides galvanic isolation on its outputs |

Bits 6 and 7 exist so firmware and the web UI can present appropriate warnings, and so a node carrying a mains
module can be visually distinguished in the control software.

### Boot sequence

```
1. Base board boots, module held in reset (MOD_nRESET low)
2. Rails come up
3. Firmware reads EEPROM at 0x50
4. Validate magic + CRC
   ├─ invalid / absent → node runs "headless": network up, no I/O, status LED indicates fault
   └─ valid → continue
5. Check declared power against remaining budget
   └─ exceeded → refuse to enable, report fault over network
6. Load the driver for module_type
7. Release MOD_nRESET
8. Advertise capabilities via mDNS TXT records and the HTTP API
```

A node with no module still joins the network and reports itself. That is deliberate: a node that has lost its
module must be findable and diagnosable remotely.

## Mechanics

**Not yet decided.** Three candidates, with the trade-offs as understood today:

### A. Board-to-board stacking header + standoffs (current preference)

Two 2×8 headers, module sits above the base board on M3 standoffs.

- Cheap, mechanically solid, hand-solderable, survives vibration and transport
- Well-defined ground return path — important for the WS2812 timing line
- Requires opening the enclosure to change a module

### B. Pogo pins

Spring contacts on the base, pads on the module; module swapped without tools.

- Best user experience for field servicing
- Contact resistance and reliability under vibration are a real concern at ~1 A
- Corrosion risk in the humid, fog-fluid-laden environment of a haunt is a genuine problem
- Needs a mechanical retention design that does the actual work of holding the module

### C. Card edge connector

- Cheapest per unit at volume, but requires gold plating and a retention mechanism
- Fine for a horizontally-inserted module in a slot-style enclosure

**Recommendation for a first prototype: option A.** It removes an entire category of risk while the electrical
interface is still being proven, and switching to B or C later is a base board revision, not an ecosystem
change — the pinout and the descriptor stay identical.

## Stacked versus remote modules

Not every module can sit on top of the base board.

**Stacked modules** — DMX, WS2812, digital inputs, OLED, sensors. Same enclosure, board-to-board connector.

**Remote modules** — anything carrying mains voltage. The creepage and clearance requirements for 230/400 V,
plus the physical size of a four-pole contactor, plus the different enclosure and earthing requirements, make
a stacked mains module impractical and inadvisable.

**How remote modules connect is an open question.** Two candidate answers:

1. **A short cable carrying only 12 V and a couple of control lines**, with the isolation provided by the
   relay's own coil-to-contact barrier. I²C is a poor choice over a cable — capacitance and noise immunity are
   both bad — so this variant would need a different signalling scheme (a simple GPIO, or RS-485).
2. **The remote actuator gets its own base board.** Consistent with the philosophy already applied to DMX
   ports ("hang another node next to it"), costs a switch port and ~$20, and completely sidesteps the cabled
   bus problem.

Option 2 is architecturally cleaner and is the current lean. See [09 — Open Questions](09-open-questions.md).
