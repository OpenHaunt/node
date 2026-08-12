# 06 — Firmware & Network

## Design principle

**A node is discovered, not configured.** The only thing an installer should have to do is plug in an Ethernet
cable. Everything else — addressing, identification, capability negotiation — happens automatically. Any
design that requires typing an IP address on a label has failed.

## Discovery

### mDNS

Nodes advertise `_openhaunt._tcp` on the local link.

```
Service:  _openhaunt._tcp.local
Instance: openhaunt-<short-serial>       e.g. openhaunt-1a2b3c
Port:     80
Hostname: openhaunt-<short-serial>.local
```

TXT records, populated from the module descriptor read at boot:

| Key | Example | Source |
|---|---|---|
| `v` | `1` | Protocol version |
| `fw` | `0.3.1` | Firmware version |
| `sn` | `1a2b3c4d` | Node serial (from ESP32 eFuse MAC) |
| `mod` | `0x0001` | Module type ID |
| `modname` | `DMX512 Out` | Module name from EEPROM |
| `modsn` | `9f8e7d6c` | Module serial from 24AA02UID |
| `modrev` | `2` | Module hardware revision |
| `caps` | `dmx,rdm` | Capability shortlist for UI filtering |
| `name` | `Coffin Room DMX` | User-assigned friendly name |

This means a control application can build a complete room inventory — what exists, what it can do, and which
physical module is in which node — from discovery alone, with no per-device configuration.

### Addressing

DHCP by default, with a static fallback configurable through the web UI. mDNS hostnames mean the DHCP lease
churn that plagues seasonal installations stops mattering.

## Control interfaces

Four, each for a different job. Resisting the urge to do everything over one protocol is deliberate.

### 1. HTTP + JSON — configuration and control

The primary interface. Node-RED speaks it natively with `http request` nodes, and it is trivially debuggable
with `curl`.

```
GET  /api/v1/info              node identity, firmware, module descriptor
GET  /api/v1/state             current state of all module I/O
POST /api/v1/state             set outputs
GET  /api/v1/config            network, name, protocol settings
POST /api/v1/config
POST /api/v1/identify          blink the status LED — find this node in the rig
POST /api/v1/ota               firmware upload
```

`/api/v1/identify` matters more than it looks. Locating one node among forty in a dark maze is otherwise a
genuine problem.

### 2. MQTT — events and state

Event-driven inputs over HTTP polling is the wrong shape. A button press should push.

```
openhaunt/<serial>/status          online / offline (retained, LWT)
openhaunt/<serial>/input/<n>       edge events, with timestamp
openhaunt/<serial>/output/<n>/set  command topic
openhaunt/<serial>/health          uptime, temperature, PoE class, errors
```

Last Will and Testament gives free failure detection: if a node drops off, the broker announces it and the
show software can react rather than silently losing an effect.

Node-RED's MQTT integration makes this the natural path for show logic.

### 3. Art-Net / sACN — DMX

For nodes carrying the DMX module. Both are supported; **sACN (E1.31) is preferred** — see the bandwidth
section below.

### 4. OSC — optional

The show-control world uses OSC widely (QLab, TouchDesigner, Isadora). Worth supporting as a thin adapter onto
the same internal command model, since integrating with an existing venue's tooling is often what decides
whether hardware gets adopted.

## Bandwidth and the broadcast problem

The traffic a node generates is negligible. The traffic it must **absorb** is the real design question.

### What one DMX universe costs

```
DMX512 with 512 slots: 512 bytes × 44 µs + break + MAB ≈ 22.6 ms → max ~44 Hz refresh

Art-Net ArtDMX on the wire:
  530 B payload + 8 UDP + 20 IP + 14 Ethernet + 4 FCS + 20 preamble/IFG ≈ 596 B
  596 B × 8 × 44 Hz ≈ 210 kbit/s per universe
```

Against a 100 Mbit/s link with an internal MAC and DMA, that is nothing.

### What the network throws at you

Art-Net consoles frequently **broadcast**. A node must receive, pass through lwIP, and discard every universe
on the network, not just the ones it uses.

| Scenario | Load at every node |
|---|---|
| 1 universe, unicast or multicast | 0.2 Mbit/s |
| Console broadcasting 16 universes | 3.4 Mbit/s |
| Console broadcasting 32 universes | 6.7 Mbit/s |
| Console broadcasting 64 universes | 13.4 Mbit/s |

With the internal RMII MAC this is absorbed without difficulty — and this is precisely the reason the internal
MAC was chosen over an SPI Ethernet controller, which would have peaked around 8–15 Mbit/s of practical
throughput. See [08 — Design Decisions](08-design-decisions.md).

### The mitigation is protocol work, not hardware

- **sACN with a correct IGMP join.** Each universe has its own multicast group; a switch with IGMP snooping
  forwards only the universes the node subscribed to. Load stays at 0.2 Mbit/s regardless of rig size. **This
  is the main reason to support sACN.**
- **Art-Net unicast** via ArtPoll/ArtPollReply discovery, so consoles target nodes directly.

## Latency

For a scare effect, the budget from trigger to visible result is on the order of 50 ms. Contributions:

| Stage | Typical |
|---|---|
| Input edge → interrupt on module expander | < 1 ms |
| I²C read + firmware handling | 1–3 ms |
| MQTT publish → broker → subscriber | 2–10 ms on a local wired network |
| Show logic decision | application dependent |
| Command → node → output | 2–5 ms |
| DMX frame boundary | up to 22.6 ms |

The DMX frame boundary dominates and is irreducible — it is the protocol. Everything else must stay
interrupt-driven; a polling loop anywhere in this chain will be felt.

**Design rule: inputs are interrupt-driven and timestamped at the node.** If the event carries the node's
timestamp, jitter downstream can be compensated rather than accumulating.

## Storage

The 16 MB external flash holds, on LittleFS:

- Web UI assets (HTML, CSS, JS)
- Recorded DMX or effect sequences for standalone playback
- A rolling event log for diagnostics
- Module descriptor cache

### Recording capacity

Raw DMX capture runs at 512 B × 44 Hz = 22.5 kB/s, so roughly **45 seconds per free megabyte**. With ~12 MB
free that is around 9 minutes of raw capture.

In practice, store **events, not frames**. Real show data is static between cues — a fade changes perhaps 20
of 512 channels over several seconds. Delta encoding or simple RLE compresses typical show data by a factor of
50–100, turning nine minutes into hours.

### Standalone operation

A node should be able to play a stored sequence with no network present. An escape room whose server has
crashed should still be able to run its lights. This is a firmware feature that the hardware already supports
and is worth designing in from the start rather than retrofitting.

## Firmware architecture

```
┌────────────────────────────────────────────────┐
│  Application: show logic, sequencer, scheduler │
├────────────────────────────────────────────────┤
│  Protocol layer: HTTP · MQTT · Art-Net · sACN  │
├────────────────────────────────────────────────┤
│  Module abstraction                            │
│    descriptor parse → driver bind → I/O model  │
├────────────────────────────────────────────────┤
│  Drivers: dmx · gpio_in · ws2812 · relay · ... │
├────────────────────────────────────────────────┤
│  ESP-IDF: lwIP · esp_eth · esp_flash · LittleFS│
└────────────────────────────────────────────────┘
```

**One firmware image for all nodes.** The module descriptor selects the driver at runtime. This keeps
distribution, OTA and version management to a single artefact — a significant operational simplification once
there are dozens of nodes in a venue.

### OTA

Two app slots in the internal 4 MB flash, ~1.95 MB each. Web UI assets live on the external flash and are
updated separately, which is what keeps the app partitions viable.

**Never ship without a working rollback path.** A node in a maze ceiling is not conveniently reachable.

### Fault behaviour

| Condition | Behaviour |
|---|---|
| No module detected | Join network, advertise as `mod=0x0000`, status LED fault pattern |
| Module power exceeds budget | Refuse to release `MOD_nRESET`, report over network |
| Network lost | Continue standalone playback if configured; report on reconnect |
| Broker lost | Retry with backoff, buffer events up to a bounded depth |
| PoE lost | Nothing to do — but the module's de-energised state is a design decision, see [05](05-module-catalog.md) |

## Node-RED integration

The intended day-one experience:

1. Plug in nodes
2. An mDNS discovery flow finds them and builds the inventory
3. MQTT input nodes subscribe to the triggers
4. Show logic in the flow editor
5. HTTP or MQTT output nodes drive the effects

No custom software required to get a room running. A purpose-built OpenHaunt server can replace Node-RED later
for a better operator experience, but the node must never *depend* on it — a node that only works with its own
proprietary controller is a node nobody will build a room around.
