# 06 — Firmware & Network

## Design principle

**A node is discovered, not configured.** The only thing an installer should have to do is plug in an Ethernet
cable. Everything else — addressing, identification, capability negotiation — happens automatically. Any
design that requires typing an IP address on a label has failed.

## Two processors, one node

The biggest change from the two-board design. There are now two firmware images.

```
┌─────────────────────────────────────────────┐
│ ESP32-S3, on the mainboard                  │
│   network via W5500, protocols, show logic  │
│   power budget enforcement, OTA for both    │
│   USB host                                  │
└───────────────────┬─────────────────────────┘
                    │ J3: USB full-speed
┌───────────────────▼─────────────────────────┐
│ RP2354, on the IO board                     │
│   timing, PIO, local timestamps, failsafe   │
│   USB device                                │
└─────────────────────────────────────────────┘
```

The division is deliberate. **Anything with a deadline lives on the RP2354; anything with a network
connection lives on the ESP32.** Nothing timing-critical crosses the link.

That rule is what buys the architecture its main benefit. The ESP32 can stall for 50 ms on a flash erase,
absorb a burst of broadcast Art-Net, or reboot for an OTA update, and the outputs keep running. On the
two-board design a wedged ESP32 was a wedged effect.

### Link protocol

USB full-speed, mainboard as host. A vendor-specific interface with bulk endpoints carries the messages
below; CDC would work too and costs nothing in driver effort on either side.

Request/response with unsolicited events from the IO board. The mainboard is always the initiator except
for events.

| Direction | Message | Purpose |
|---|---|---|
| → | `IDENTIFY` | Read the IO board descriptor. First thing after reset release |
| → | `SET` | Set outputs. Channel-addressed |
| → | `GET` | Read inputs |
| → | `CONFIG` | Channel modes, debounce, failsafe state, failsafe timeout |
| → | `PING` | Link keepalive |
| → | `FW_*` | Firmware update to the resident bootloader |
| ← | `EVENT` | Input edge, with an RP2354-local timestamp |
| ← | `FAULT` | Self-test failure, overtemperature, output fault |

`IO_ATTN` exists so that the IO board can pull the mainboard's attention without waiting for the next USB
poll. USB is host-polled by definition, so a device cannot speak unprompted; a sideband interrupt line
gives sub-millisecond notice of an edge without polling the bulk endpoint at high frequency.

### Failsafe

The IO board holds its last commanded state by default. `CONFIG` can give it a failsafe state and a
timeout, after which it falls back if the link goes quiet.

**The right timeout is not obvious and is a per-board decision.** A dry contact driving a fog machine
probably should drop out after a few seconds of silence. A relay holding work lights on probably should
not, because "the lights went out because the ESP32 rebooted" is worse than the fault it was protecting
against. The default is to hold, and `CONFIG` is how a board or an installation opts out of that.

### Firmware update for both processors

One artefact, two images. The ESP32 image and the RP2354 image ship together and are version-locked,
because the link protocol is a compatibility surface between them and letting them drift independently
would be a support problem nobody wants.

```
1. Node receives a bundle over /api/v1/ota
2. RP2354 image is written to the external flash on the mainboard
3. ESP32 image goes into the inactive OTA slot
4. ESP32 reboots into the new image
5. New ESP32 firmware compares the RP2354's reported fw_version against the bundled image
   └─ mismatch → push it over the link to the IO board's resident bootloader
6. IO board verifies, switches slot, reboots, re-identifies
```

Step 2 is why the external flash matters more than it did. It is no longer only web UI assets and
recordings; it holds the IO board images the mainboard serves.

**The ROM bootloader is the recovery path.** With a USB link the mainboard can assert `IO_BOOTSEL`, cycle
`IO_nRESET`, and the RP2354 enumerates as mass storage accepting a UF2 image. A resident bootloader is still
worth having so routine updates do not need a reset dance, but it is no longer the only thing between a
failed update and an SWD probe.

## Discovery

### mDNS

Nodes advertise `_openhaunt._tcp` on the local link.

```
Service:  _openhaunt._tcp.local
Instance: openhaunt-<short-serial>       e.g. openhaunt-1a2b3c
Port:     80
Hostname: openhaunt-<short-serial>.local
```

TXT records, populated from both board descriptors read at boot:

| Key | Example | Source |
|---|---|---|
| `v` | `1` | Protocol version |
| `fw` | `0.3.1` | Firmware version |
| `sn` | `1a2b3c4d` | Node serial (from ESP32 eFuse MAC) |
| `io` | `0x0001` | IO board type ID |
| `ioname` | `DMX512 Out` | IO board name, from the link descriptor |
| `iosn` | `9f8e7d6c` | IO board serial, from the RP2350 chip ID |
| `iorev` | `2` | IO board hardware revision |
| `pwr` | `0x0003` | Power board type ID |
| `class` | `8` | Declared PoE class |
| `budget` | `83000` | Power available to the IO board, in mW |
| `caps` | `dmx,rdm` | Capability shortlist for UI filtering |
| `name` | `Coffin Room DMX` | User-assigned friendly name |

A control application can build a complete room inventory from discovery alone, with no per-device
configuration: what exists, what it can do, which physical boards are in which node, and how much power
each one has left.

### Addressing

DHCP by default, with a static fallback configurable through the web UI. mDNS hostnames mean the DHCP lease
churn that plagues seasonal installations stops mattering.

## Control interfaces

Four, each for a different job. Resisting the urge to do everything over one protocol is deliberate.

### 1. HTTP + JSON — configuration and control

The primary interface. Node-RED speaks it natively with `http request` nodes, and it is trivially debuggable
with `curl`.

```
GET  /api/v1/info              node identity, both firmware versions, both board descriptors
GET  /api/v1/state             current state of all IO board channels
POST /api/v1/state             set outputs
GET  /api/v1/power             per-rail voltage, current, power, and the budget
GET  /api/v1/config            network, name, protocol settings
POST /api/v1/config
POST /api/v1/identify          flash the etherCON Halo ring, find this node in the rig
POST /api/v1/ota               firmware bundle upload, both processors
```

`/api/v1/identify` matters more than it looks. Locating one node among forty in a dark maze is otherwise a
genuine problem, and the Halo light ring around the etherCON is the right indicator for it because it is on
the outside of the box, at the connector, where the installer is already looking.

`/api/v1/power` is new and exists because the metering is new. It is also the answer to "why did that node
not come up", which on a PoE installation is usually a switch budget question rather than a node fault.

### 2. MQTT — events and state

Event-driven inputs over HTTP polling is the wrong shape. A button press should push.

```
openhaunt/<serial>/status          online / offline (retained, LWT)
openhaunt/<serial>/input/<n>       edge events, with timestamp
openhaunt/<serial>/output/<n>/set  command topic
openhaunt/<serial>/health          uptime, temperature, PoE class, errors
openhaunt/<serial>/power           per-rail V/I/W, budget headroom, eFuse trips
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

**This generation cannot absorb the worst of that.** The W5500 over SPI peaks around 8 to 15 Mbit/s of
practical throughput, so a console broadcasting 32 universes or more saturates the link. That is the price
of D32, paid to get a USB host for the IO board interface, and it is why sACN is required rather than
preferred. Generation 2 returns to an internal MAC and the problem disappears. See D32 and D33 in
[10 — Design Decisions](10-design-decisions.md).

### The mitigation is protocol work, not hardware

- **sACN with a correct IGMP join.** Each universe has its own multicast group; a switch with IGMP snooping
  forwards only the universes the node subscribed to. Load stays at 0.2 Mbit/s regardless of rig size.
  **With the W5500 this is no longer an optimisation but the only way a large rig works at all.**
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
ESP32, mainboard
┌────────────────────────────────────────────────┐
│  Application: show logic, sequencer, scheduler │
├────────────────────────────────────────────────┤
│  Protocol layer: HTTP · MQTT · Art-Net · sACN  │
├────────────────────────────────────────────────┤
│  Board abstraction                             │
│    descriptors → driver bind → channel model   │
├────────────────────────────────────────────────┤
│  Power manager: metering, budget, eFuse alerts │
├────────────────────────────────────────────────┤
│  Link master: framing, CRC, retry, FW push     │
├────────────────────────────────────────────────┤
│  ESP-IDF: lwIP · esp_eth · esp_flash · LittleFS│
└───────────────────────┬────────────────────────┘
                        │ J3
RP2354, IO board        │
┌───────────────────────▼────────────────────────┐
│  Link slave: framing, CRC, event queue         │
├────────────────────────────────────────────────┤
│  Channel engine: modes, debounce, failsafe     │
├────────────────────────────────────────────────┤
│  PIO programs: dmx · ws2812 · encoder · servo  │
├────────────────────────────────────────────────┤
│  Resident bootloader (separate flash slot)     │
└────────────────────────────────────────────────┘
```

**One firmware bundle for all nodes.** The two descriptors select the drivers at runtime, so distribution,
OTA and version management stay a single artefact even with two processors and a growing catalogue of
boards. That is a real operational simplification once a venue has dozens of nodes, and it is worth
protecting: resist any temptation to ship board-specific ESP32 builds.

### OTA

Two app slots in the internal 4 MB flash, ~1.95 MB each. Web UI assets live on the external flash and are
updated separately, which is what keeps the app partitions viable.

**Never ship without a working rollback path.** A node in a maze ceiling is not conveniently reachable.

### Fault behaviour

| Condition | Behaviour |
|---|---|
| No power board, or a bad descriptor | Run on 3.3 V, join network, report fault, enable nothing |
| No IO board detected | Join network, advertise `io=0x0000`, report fault |
| IO board needs an absent rail | Hold in reset, report which rail, do not brown out trying |
| IO board draw exceeds budget | Hold in reset, report the declared and available figures |
| eFuse trip on a rail | `PWR_ALERT` asserts, log it, report it, attempt one reset of that rail |
| Link timeout | IO board applies its configured failsafe. ESP32 retries, then resets the IO board |
| ESP32 reboot or OTA | **Outputs hold.** The IO board is not reset by an ESP32 restart |
| Network lost | Continue standalone playback if configured, report on reconnect |
| Broker lost | Retry with backoff, buffer events to a bounded depth |
| PoE lost | Nothing to do, but the IO board's de-energised state is a design decision |

The two rows in the middle are the ones worth arguing about. **Refusing to start is better than browning
out**, because a node that declines to enable an IO board and says exactly why is diagnosable from a laptop,
and one that resets in a loop mid-show is not.

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
