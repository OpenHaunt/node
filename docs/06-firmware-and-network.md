# 06 — Firmware & Network

## Design principles

**A node is discovered, not configured.** The only thing an installer should have to do is plug in an Ethernet
cable. Everything else — addressing, identification, capability negotiation — happens automatically. Any
design that requires typing an IP address on a label has failed.

**A node describes itself; a controller carries no module catalogue.** Only the device knows what it is and
how it is best controlled. A node says what terminals it has — how many, which way each one flows, what kind
of value it carries, what unit and what range — and a controller builds its idea of the device from that
answer alone. It does not keep a table from module type id to "eight contacts" or "a colour and a
brightness", and nothing in this document asks it to.

The consequence is the point: a module newer than the controller works, a third-party module works, and a
module whose firmware grows a port works, none of them requiring anybody to ship new controller software. The
alternative — every controller carrying a copy of [05 — Module Catalog](05-module-catalog.md) — makes the
catalogue a compatibility surface, and every entry in it a thing that can be out of date somewhere.

The vocabulary is borrowed from **E1.73 UDR**, by way of [SHIFTY](https://github.com/oshifty/vision), which
takes the same position: `access`, `dataType`, `unit`, `minimum`/`maximum`/`default` mean here what they mean
there. Using an existing spelling is worth more than inventing a better one.

The description is produced by the firmware's module driver from the descriptor's type id — the driver knows
its own ports, and it is already bound at runtime by the module abstraction below. The 256-byte EEPROM in
[04 — Module Interface](04-module-interface.md) is untouched: it stays an identity and a capability bitfield,
not a schema, and nothing here needs it to grow.

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
| `caps` | `dmx,rdm,sacn` | Capability shortlist for UI filtering |
| `mains` | `1` | Present and `1` when the module switches mains — descriptor bit 6 |
| `name` | `Coffin Room DMX` | User-assigned friendly name |

`mains` is duplicated from the module descriptor deliberately. A control application should be able to put a
mains warning next to a node the moment it appears, and reading capability bit 6 over HTTP means the warning
arrives a round trip after the device does. That is the wrong order for the one property where being early
matters. Everything else in this table is a convenience; this one is a safety affordance.

This means a control application can build a complete room inventory — what exists, what it can do, and which
physical module is in which node — from discovery alone, with no per-device configuration.

`mod` and `caps` are an inventory shortlist and nothing more: they are for filtering a list, grouping an
inventory, and putting the mains warning up early. They are not the basis for control. What a node can
actually be told to do comes from `ports` in `GET /api/v1/info`, below.

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

## Payloads

The tables above name every endpoint and topic. What goes *inside* them is specified here, because two
implementations that agree on a topic and disagree on its body fail in the least helpful way available: the
message arrives, is parsed, and means nothing.

The shapes follow from the **port description**, not from the module. A node says a port is a `boolean` and
that port carries `{ "state": … }`; it says `number` and the port carries `{ "value": … }`. There is one table
of payloads, indexed by data type, and it is short. A controller that has read `ports` knows what to send
without knowing what a `0x0003` is.

These shapes come from [the-pult](https://github.com/RuegerEvents/the-pult), a control application that
speaks this protocol end to end against a simulated node. They are offered as the specification, not as a
fait accompli — where firmware wants something different, the firmware is right and this document should
change.

### `GET /api/v1/info`

```json
{
  "v": "1",
  "fw": "0.3.1",
  "sn": "1a2b3c4d",
  "name": "Coffin Room LED",
  "module": {
    "type": "0x0003",
    "name": "WS2812",
    "sn": "9f8e7d6c",
    "rev": "2",
    "flags": 0,
    "caps": ""
  },
  "ports": [
    { "port": 0, "name": "Strip colour", "access": "readwrite", "dataType": "color", "class": "color" },
    { "port": 1, "name": "Brightness",   "access": "readwrite", "dataType": "number",
      "unit": "percent", "minimum": 0, "maximum": 1, "default": 0, "class": "intensity" }
  ]
}
```

`module.flags` is the capability bitfield from the module descriptor, verbatim. Bit 6 is the mains section.
A control application reads it here to confirm what the `mains` TXT key claimed.

#### `ports`

**This is how a controller learns what the node can do.** One entry per terminal, in the node's own port
numbering — the same `<n>` that appears in its topics and in `/api/v1/state`.

| Field | Values | Notes |
|---|---|---|
| `port` | integer | The `<n>` in the topics and in `/state`. Required. |
| `name` | string | Friendly, and shown to an operator. Required. |
| `access` | `readonly` \| `readwrite` | UDR. `readonly` means the node produces the value — a controller **input**. `readwrite` means the controller drives it — an **output**. Required. |
| `dataType` | `boolean` \| `number` \| `string` \| `color` | UDR's enum plus `color`, which UDR does not have. The one extension, and the only one. Required. |
| `unit` | UDR unit name — `degree-celsius`, `percent`, `parts-per-million`, `unitless` | Numbers only. Optional. |
| `minimum` / `maximum` | number | The range the port works in. Optional. |
| `default` | number | Where the port sits before anything drives it; `0` or `1` on a boolean. Optional. |
| `class` | see below | A hint at what the port *means*. Optional. |

`class` is a small vocabulary a controller may recognise, for the places where it needs semantics rather than
a value: drawing a temperature on a floor plan, wiring an intensity into a fader, knowing a colour is a
colour. The words are `contact`, `switch`, `temperature`, `humidity`, `air-quality`, `intensity`, `color`,
`text`.

**A `class` a controller does not recognise, or an absent one, is not an error.** It means the port has no
special meaning to that controller, which should then treat it as a parameter named after `name` and drive it
by its `dataType`. This is what makes a module nobody has heard of usable rather than merely visible, and it
is why `class` is a hint and not a required field: a node should feel free to declare a port that is exactly
what it is and nothing the vocabulary covers.

A controller that cannot get a description should refuse to adopt the node rather than guess. There is
nothing to guess *from* — that is the design principle working.

#### `dmx`

Present only on a node that forwards a universe. Its presence is what tells a controller to allocate one and
send `dmx` in `/api/v1/config`; its absence means there is none.

```json
"dmx": { "protocols": ["sacn"], "universes": 1 }
```

`protocols` lists what the node will accept, in the same words `/api/v1/config` takes. `universes` is how many
it forwards.

### `POST /api/v1/config`

Partial. Only the keys present are changed; everything else is left alone. Applied immediately and persisted
across reboots.

```json
{
  "mqtt": { "broker": "10.0.0.5:1883" },
  "dmx":  { "protocol": "sacn", "universe": 5 }
}
```

`dmx` is accepted only on a node whose description declares `dmx`; anywhere else it is meaningless and a node
should say so rather than quietly forwarding nothing. `protocol` is one of the words that node's
`dmx.protocols` listed.

This is the only configuration a node ever receives, and it arrives when a control application adopts it. A
node that has not been adopted has been asked for nothing, which is the design principle working: discovery
requires no configuration, and configuration begins only when somebody decides to use the node.

A node that reboots has forgotten this, and re-announcing itself over mDNS is the only notice a controller
gets. Controllers should re-send config on every resolve for a node they have adopted; nodes should expect it.

### `POST /api/v1/state`

Outputs keyed by port number, as strings — the same numbering the description used. Payloads are the
per-`dataType` shapes below.

```json
{ "outputs": { "0": { "state": true } } }
```

`GET /api/v1/state` answers in the same shape, with every port the module has.

### `openhaunt/<sn>/status`

The literal strings `online` and `offline`. Not JSON — it is also the Last Will, and a will is more useful the
simpler it is. Retained, so a controller that connects late still learns the truth.

### `openhaunt/<sn>/input/<n>`

Two shapes on one topic, because from a show's point of view a contact closing and a sensor reporting are the
same event: a value the device produced.

```json
{ "state": true, "edge": "rising", "ts": 1699999999999 }
{ "value": 21.5, "unit": "degree-celsius", "ts": 1699999999999 }
```

Which of the two a port publishes follows its `dataType`, exactly as it does for outputs: a `boolean` port
carries `state`, a `number` port carries `value`.

`edge` is `rising` or `falling`. `unit` is the same UDR name the port's description gave, repeated on the
reading so that a message is readable on its own. `ts` is the node's own clock in milliseconds — worth
carrying because it is closer to when the thing happened than the moment a controller got round to reading its
socket.

Which port is which is not something a controller should have to know: the description said, and it said with
names and units.

### `openhaunt/<sn>/output/<n>/set`

One shape per `dataType`, and that is the whole table:

| `dataType` | Payload |
|---|---|
| `boolean` | `{ "state": true }` |
| `number` | `{ "value": 0.5 }` — within the port's `minimum`/`maximum`, in its `unit` |
| `color` | `{ "r": 255, "g": 128, "b": 0 }` — bytes |
| `string` | `{ "text": "ACT ONE" }` |

There is no `{ "brightness": … }`. A strip's brightness is a `number` port in `percent`, and it takes `value`
like every other number.

#### What the catalogue modules would say

An example, not a contract. These are the descriptions the firmware drivers for [05](05-module-catalog.md)
would produce, written out so there is something concrete to read — a controller must take them from the node
and not from here.

| Module | Ports |
|---|---|
| `0x0001` DMX512 out | none; `dmx: { protocols: ["sacn", "artnet"], universes: 1 }` |
| `0x0002` Digital input | 0–7, `readonly` `boolean`, `class: contact` |
| `0x0003` WS2812 | 0 `readwrite` `color` `class: color`; 1 `readwrite` `number` `percent` 0–1 `class: intensity` |
| `0x0004` Mains relay | 0 `readwrite` `boolean` `class: switch` |
| `0x0005` OLED | 0 `readwrite` `string` `class: text` |
| `0x0006` Dry contact | 0–3, `readwrite` `boolean`, `class: switch` |
| `0x0007` Environmental | 0 temperature `degree-celsius`; 1 humidity `percent`; 2 air quality `parts-per-million` — all `readonly` `number` |

`0x0007`'s port mapping was an open question in this document's first draft. It stops being one: the node
says, so nobody has to agree in advance.

Per-pixel control of `0x0003` is deliberately absent. Addressing 300 pixels through MQTT at frame rate is the
wrong shape, and the right one is probably a DMX or sACN universe mapped onto the strip. Left open until
somebody needs it.

### `openhaunt/<sn>/health`

```json
{ "uptime_s": 4210, "temp_c": 41.5, "poe_class": 3, "errors": [] }
```

Everything but `uptime_s` is optional. `errors` is a list of short machine-readable strings (`i2c`, `dmx-break`)
rather than prose — a controller should be able to count and group them.

### sACN to the DMX module

A node that declared `dmx` listens on UDP 5568 for the universe it was given in `/api/v1/config`. **Unicast is
expected** in addition to the multicast group, which is the argument in the next section taken to its
conclusion: a controller that knows exactly where a node is, and which universe it wants, has no reason to
make anything else on the network look at the packet. `dmx.protocols` says what it will accept; `caps` should
list `sacn` too, so a controller can filter its inventory before it asks.

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
