# 02 — System Architecture

## Block diagram

```
╔═══ DOMAIN 1 ═════════════╗           ╔═══ DOMAIN 2: Logic (GND_A) ══════════════════╗
║  Ethernet cable / PoE    ║           ║                                              ║
║                          ║           ║   ┌──────────────────┐                       ║
║  ┌────────────────────┐  ║  TX±/RX±  ║   │  RTL8201FI-VC-CG │                       ║
║  │  RJ45 magjack      │══╬═══════════╬══▶│  Ethernet PHY    │                       ║
║  │  PoE-rated         │  ║           ║   └────────┬─────────┘                       ║
║  │  ┌─ centre taps    │  ║           ║            │ RMII 6 + MDC/MDIO 2             ║
║  └──┼─────────────────┘  ║           ║            │                                 ║
║     │                    ║           ║   ┌────────▼──────────────────────┐          ║
║     ▼                    ║           ║   │   ESP32-WROOM-32UE-N4         │          ║
║  ┌────────────────────┐  ║   12 V    ║   │                               │          ║
║  │  Silvertel Ag9712  │══╬═══════════╬══▶│   50 MHz OSC ──▶ GPIO0        │          ║
║  │  802.3af, 12 W     │  ║           ║   │   GPIO16 ──▶ OSC enable       │          ║
║  └────────────────────┘  ║           ║   └───┬───────────────────┬───────┘          ║
║      ▲ 1500 V            ║           ║       │ SPI               │ module bus       ║
╚══════╬═══════════════════╝           ║  ┌────▼────────┐          │                  ║
       ║                               ║  │ W25Q128JV   │          │                  ║
       ║ isolation inside the module   ║  │ 16 MB       │          │                  ║
       ║                               ║  └─────────────┘          │                  ║
       ║                               ║                           │                  ║
       ║              12 V ────────────╬─▶ AP63205 ─▶ 5 V          │                  ║
       ║                               ║   AP63203 ─▶ 3.3 V        │                  ║
       ║                               ╚═══════════════════════════╬══════════════════╝
       ║                                                           │
       ║                               ╔═══════════════════════════▼══════════════════╗
       ║                               ║        MODULE CONNECTOR (2×8, 2.54 mm)       ║
       ║                               ║   12 V · 5 V · 3.3 V · GND                   ║
       ║                               ║   I²C · UART+DE · FAST0 · INT · nRESET       ║
       ║                               ╠══════════════════════════════════════════════╣
       ║                               ║        MODULE  ── optional DOMAIN 3 ──▶      ║
       ╚═══════════════════════════════╩══════════════════════════════════════════════╝
                   Domain 1 and any module-side domain must never touch,
                          including through the enclosure.
```

## Isolation domains

The base board deliberately has only **two** domains. Any further isolation is the module's responsibility,
and lives entirely on the module.

### Domain 1 — Ethernet cable and PoE input

Everything on the cable side of the magjack and the cable side of the PoE module's transformer. Isolated from
the rest of the board by:

- the magjack magnetics (data pairs), 1500 V
- the Silvertel module's internal transformer (power), 1500 V

### Domain 2 — Logic (GND_A)

The output of the PoE module and everything it feeds: processor, PHY, flash, both bucks, and the module
connector. This is the domain the module bus lives in, and it is **always SELV**.

### Domain 3+ — Module-side domains (optional, per module)

A module may add its own barrier. Two examples:

- **DMX module:** the CA-IS2092VW provides 3750 V between the logic side and the DMX cable side. The XLR
  shell/pin 1 and the TVS reference sit in that isolated domain (GND_B).
- **Mains module:** the relay or contactor itself is the barrier. Reinforced insulation between coil and
  contacts (typically 5 kV, ≥8 mm creepage) keeps the module's control side SELV while mains exists only on
  the contact side.

**This is the architectural rule that makes the whole thing work:** the base board never sees anything but
SELV, no matter what module is attached.

### The enclosure trap

With a metal enclosure, the RJ45 shield and any module's cable shield can bond to chassis and short two
domains together, undoing barriers that exist on the PCB. The chassis bonding scheme has to be a deliberate
decision documented per enclosure variant, not an accident of mechanical assembly. See
[09 — Open Questions](09-open-questions.md).

## Power tree

```
PoE 48 V (36–57 V)
   │
   ▼
Silvertel Ag9712-2BR ──▶ 12 V @ 1.0 A (12 W)        802.3af, 1500 V isolated
   │                                                 integrated bridge rectifiers
   ├──────────────────────────────▶ MODULE 12 V     ~850 mA available
   │
   ├──▶ AP63205WU-7 ──▶ 5.0 V ──┬──▶ MODULE 5 V
   │                             └──▶ (base board 5 V loads, if any)
   │
   └──▶ AP63203WU-7 ──▶ 3.3 V ──┬──▶ ESP32-WROOM-32UE   ~120 mA
                                 ├──▶ RTL8201FI          ~60 mA
                                 ├──▶ W25Q128JV          ~5 mA
                                 └──▶ MODULE 3.3 V
```

### Budget

| Consumer | Rail | Typical |
|---|---|---|
| ESP32 (Ethernet active, Wi-Fi off) | 3.3 V | ~120 mA |
| RTL8201FI Ethernet PHY | 3.3 V | ~60 mA |
| W25Q128JV flash | 3.3 V | ~5 mA |
| Status LEDs, misc | 3.3 V | ~20 mA |
| **Base board total** | | **~1.3 W** |
| Conversion losses | | ~0.3 W |
| **Available to modules** | | **~10.4 W ≈ 850 mA at 12 V** |

The three module rails share one budget — they are not additive. A module drawing 500 mA at 5 V is consuming
~2.7 W of the ~10.4 W, not "500 mA out of a separate 5 V allowance".

### The 802.3af ceiling

802.3af makes 12.95 W available at the powered device. The Ag9712 delivers 12 W. **There is no headroom
above this without changing standard.** If a module needs more — a long WS2812 run, a large contactor, a
motor — the answer is 802.3at (Silvertel Ag5400/Ag5300 series, 24 W continuous / 30 W peak), and that is a
base board respin, not a component swap: different module footprint, different magjack rating, different
thermal design.

**Decide the maximum module power before layout.** See [09 — Open Questions](09-open-questions.md).

### Classification

The Ag9700 family presents a **Class 0 signature by default**, which makes the switch reserve **15.4 W per
port** for a device that actually draws 1.3–4 W. On a 370 W switch that is the difference between roughly 24
and 90 nodes.

For a product whose scaling story is literally "hang another one next to it", this matters. The **-FL**
variant of the Silvertel module supports classification programming and should be strongly considered. See
[08 — Design Decisions](08-design-decisions.md).

## Thermal

Everything dissipates into a sealed enclosure with no forced airflow:

| Source | Power |
|---|---|
| PoE module (≈88 % efficient at full load) | up to ~1.5 W |
| Base board electronics | ~1.3 W |
| Buck converter losses | ~0.3 W |
| Module (varies — relay coils are the worst case) | 0 – 2 W+ |

In a metal enclosure in a venue at 40–50 °C ambient, internal temperatures of 65–75 °C are realistic. This is
why the **industrial-temperature Ethernet PHY** was selected, and it is the reason to check every part's
temperature grade rather than assuming 85 °C is generous. It also means continuously-energised relay coils are
a genuine thermal design input, not a rounding error.

## Network architecture

```
        ┌──────────────┐
        │ PoE Switch   │──── Node (DMX module)      ──▶ XLR ──▶ fixtures
        │ + IGMP       │──── Node (relay module)    ──▶ mains ──▶ fog machine
        │   snooping   │──── Node (input module)    ◀── reed switch, PIR, button
        └──────┬───────┘──── Node (WS2812 module)   ──▶ LED strip
               │
        ┌──────▼──────────────────────┐
        │ Node-RED / OpenHaunt server │  mDNS discovery, HTTP/JSON, MQTT
        │ (a mini PC or a Pi)         │  Art-Net / sACN source for DMX nodes
        └─────────────────────────────┘
```

There is no daisy-chaining and no bus. Every node is a switch port. This costs ports but buys complete
independence: a failed node cannot take down its neighbours, and troubleshooting is per-device.

**Use a switch with IGMP snooping.** With sACN multicast, the switch then delivers only the universes each
node actually subscribed to. See [06 — Firmware & Network](06-firmware-and-network.md) for why this matters
more than raw bandwidth.
