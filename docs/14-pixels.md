# 14 — Pixel strips, and heads

A node's WS2812 output runs in one of two **modes**, chosen when the strip is patched and kept
for the whole night:

- **`effect`**: the node draws every pixel itself, from a handful of parameters the console
  sets like any other port (colour, second colour, brightness, effect, speed, size, reverse,
  mirror, segments). A chase is one message, and then nothing.
- **`sacn`**: the console draws every pixel, and the node maps one or more sACN universes onto
  the strip.

A strip in either mode is its own fixture to the console: a **head**. Heads are what this
extension adds to the protocol, and they come first in this document because the strips need
them.

Like doc 13, this is an extension to doc 08. A node that says nothing about heads or pixels is
driven exactly as before. The modes are not negotiated: the console sets one, and the node
describes the result.

## Heads

A node with two strips has two colours and two brightnesses. The console turns a port's
`class` into a parameter kind (`color` → the fixture's colour, `intensity` → its dimmer), and
a fixture has one colour, so two strips on one fixture collide on the same key. The V0 board
worked round this by giving only strip 1 the classes. That isn't an answer.

The answer is that a port can say which **head** it belongs to:

```json
"ports": [
  { "port": 0, "name": "Relay 1", "access": "readwrite", "dataType": "boolean", "class": "switch" },
  { "port": 6, "name": "Colour", "access": "readwrite", "dataType": "color", "class": "color", "head": 1 },
  { "port": 7, "name": "Brightness", "access": "readwrite", "dataType": "number", "class": "intensity",
    "unit": "percent", "minimum": 0, "maximum": 1, "default": 0, "head": 1 }
],
"heads": [
  { "head": 1, "name": "Strip 1", "kind": "pixel-strip" },
  { "head": 2, "name": "Strip 2", "kind": "pixel-strip" }
]
```

- `head` on a port is a small integer, 1..15. **Absent means head 0**, the node itself, which is
  every port a node has ever described.
- `heads` at the top level of `GET /api/v1/info` names each head other than 0. If it's absent,
  the node has only head 0. A head may be listed and own no ports: a strip in `sacn` mode
  does (see below).
- **Classes are unique per head, not per node.** Port numbers stay unique per node, and topics
  stay `output/<port>`, so nothing on MQTT changes.
- `kind` is a hint, like `class`. `pixel-strip` is the one this document defines: the head is
  one of the strips in the `pixels` object, the one with the same `head`.

**What a console does with it:** one fixture per head, all on the same node. Head 0 is the node's
own fixture as today, and head *n* is a fixture whose ports are the ones marked `"head": n`.
A console that predates heads sees every port on one fixture, with the duplicate classes, so
it needs to be head-aware to drive a V0 strip properly.

**Telling the console the description changed:** the mDNS TXT record gains `desc`, 4 hex
digits of CRC-16/CCITT-FALSE over the node's `info` JSON with its identity (`fw`, `sn`, `name`)
blanked: the module, the ports, the heads, and the `pixels` object. The `audio` object is
left out, because its free space changes with every upload. A console refetches `/api/v1/info` when `desc` changes. Changing a strip's mode
changes its ports, and this is how the console finds out without polling.

## What a node says about its strips

`GET /api/v1/info` gains a top-level `pixels` object when the node has a strip output:

```json
"pixels": {
  "effects": ["solid", "gradient", "chase", "comet", "wave", "breathe",
              "fire", "candle", "sparkle", "lightning"],
  "orders": ["RGB", "RBG", "GRB", "GBR", "BRG", "BGR", "RGBW", "GRBW"],
  "max_count": 1024,
  "multicast_universes": 4,
  "strips": [
    { "output": 0, "head": 1, "mode": "effect", "count": 60, "order": "GRB",
      "universe": 0, "address": 1, "hold_ms": 10000, "universes": [], "max_fps": 40 },
    { "output": 1, "head": 2, "mode": "sacn", "count": 300, "order": "GRB",
      "universe": 5, "address": 1, "hold_ms": 10000, "universes": [5, 6], "max_fps": 40 }
  ]
}
```

- `output` is the strip output on the board, 0 or 1 on V0. It is the `<n>` in
  `/api/v1/pixels/<n>`.
- `head` is the head the strip is.
- `count` is how many pixels the node drives, 1..`max_count`.
- `order` is the colour order of the pixels on the wire: any permutation of `RGB`, or `RGBW`/`GRBW`
  for a four-channel strip. Every four-channel order this node can drive is listed.
- `universe` and `address`: in `sacn` mode, where pixel 0's first channel sits (`universe` 1..63999,
  `address` 1..512). They are kept, but unused, in `effect` mode.
- `universes` lists every universe the strip reads, in order (see *Spilling*). It is empty in
  `effect` mode.
- `hold_ms` is how long a strip holds its last frame when every sACN source has gone.
- `max_fps` is the most frames a second this strip can be refreshed at, from its length (see *Rate*).
- `multicast_universes` is how many universes this node can join by multicast at once, for all its
  strips together. Universes beyond that are still received if they are sent **unicast** to the node.

`GET /api/v1/pixels` answers the same object, plus a `status` per strip:

```json
"status": { "frames": 18233, "sources": 1, "holding": false, "fps": 39.8 }
```

## Setting a strip's mode

```
PUT /api/v1/pixels/<output>
{ "mode": "sacn", "count": 300, "order": "GRB", "universe": 5, "address": 1, "hold_ms": 10000 }
```

- Every key is optional. What is sent is merged over the strip's current settings.
- The node answers 200 with the strip's new object (as in `pixels.strips`), 400 with
  `{"error": "..."}` for anything out of range, and 404 for an output it doesn't have.
- The setting is **stored** (NVS) and survives reboot and OTA. A factory reset clears it.
- Applying it rebuilds the node's description: a strip in `effect` mode owns its nine ports, and a
  strip in `sacn` mode owns none. The node rebinds its I/O, as applying a virtual module does, so
  every output on the node is driven from its current value again. It updates `desc` in mDNS.
- A **stored virtual module wins** over the board's own I/O (doc 12). Such a node has no `pixels`
  object, and the endpoint answers 409.

Defaults: `effect`, 60 pixels, `GRB`, universe 0, address 1, `hold_ms` 10000.

## `effect` mode: the ports

A strip in `effect` mode is head *n* with nine ports. On V0, strip 1 (output 0, head 1) is ports
6–14 and strip 2 (output 1, head 2) is ports 15–23, in this order:

| Offset | Name | dataType | Range, unit | Default | class | Node-side effects |
|---|---|---|---|---|---|---|
| +0 | Colour | color | — | 0,0,0 | `color` | shapes, steps, transitions |
| +1 | Brightness | number | 0..1, `percent` | 0 | `intensity` | shapes, steps, transitions |
| +2 | Colour 2 | color | — | 0,0,0 | — | shapes, steps, transitions |
| +3 | Effect | string | one of `pixels.effects` | `solid` | `pixel-effect` | — |
| +4 | Speed | number | 0..10, `hertz` | 1 | `speed` | shapes, steps, transitions |
| +5 | Size | number | 0..1, `percent` | 0.25 | `size` | shapes, steps, transitions |
| +6 | Reverse | boolean | — | 0 | `reverse` | square, steps |
| +7 | Mirror | boolean | — | 0 | `mirror` | square, steps |
| +8 | Segments | number | 1..16, `unitless` | 1 | `segments` | steps, transitions |

- Every pixel's colour is computed, multiplied by Brightness, and sent. There is no gamma
  curve on the node: what the console sends is what the strip gets, as before.
- An Effect string the node doesn't know draws as `solid`, and the node raises the
  `pixel-effect` error until a known one arrives.
- Segments is rounded to an integer.
- The `effects` capability on the parameter ports is the ordinary doc 08 extension, so a console can
  fade the Speed or breathe the Colour on the node as well, and the strip's own effect runs on top
  of that.

### The effects

Positions run along a **segment**. The strip is divided into `Segments` equal segments, and every
segment draws the same pattern. *x* is a pixel's position in its segment, 0 at the start and 1 at
the end, measured at the pixel's centre. **Mirror** folds the segment about its middle (the pattern
runs from both ends towards the centre), and **Reverse** then runs it the other way.

*Phase* goes round once per cycle at `Speed` cycles a second. *Level* blends Colour 2 (at 0)
into Colour (at 1).

| Effect | What it draws |
|---|---|
| `solid` | Colour everywhere. Speed and Size do nothing. This is the old strip, and the default. |
| `gradient` | Colour at the start of the segment to Colour 2 at its end, scrolling at Speed. At Speed 0 it stands still. |
| `chase` | A block of Colour, Size long (as a fraction of the segment), on Colour 2, travelling one segment per cycle. |
| `comet` | A head of Colour with a tail Size long, fading quadratically into Colour 2. |
| `wave` | A sine of level along the segment, `1/Size` waves across it, travelling one wavelength per cycle. |
| `breathe` | The whole segment rises and falls from Colour 2 to Colour, once per cycle. |
| `fire` | Flame: value noise along the strip and in time, hotter at the start of the segment. Black → Colour (half heat) → Colour 2 (full heat). Speed is how fast it flickers, Size how big the tongues are. Set Colour red and Colour 2 yellow. |
| `candle` | Each segment is one candle, flickering on its own: Colour at a level that wanders between about 0.55 and 1. Speed is how restless it is. |
| `sparkle` | Pixels of Colour flash up and fade, on Colour 2. Size is the density (the fraction of pixels lit per flash period). Speed is flashes per second. |
| `lightning` | The whole strip is Colour 2 with an occasional strike of Colour: two or three flashes 40–80 ms long. Each `1/Speed` seconds a strike happens with probability Size. |

**Two nodes agree.** Everything is computed from the console's clock (`openhaunt/clock`):

- The random effects (`fire`, `candle`, `sparkle`, `lightning`) are a pure function of the
  console time and the pixel, so two strips told the same thing at the same time draw the
  same frame.
- The phase effects keep the phase continuous when Speed changes, so fading Speed never
  makes the pattern jump. At a steady Speed the phase is pulled gently (2% of the difference
  a frame) towards `time × Speed`, so two strips that were started apart lock together within a
  few seconds.

A four-channel strip in `effect` mode leaves W at 0.

## `sacn` mode: the mapping

The strip reads E1.31 on UDP 5568, unicast and by multicast (239.255.*hi*.*lo*), the same receiver
the DMX gateway uses.

### Spilling

Pixel 0 starts at `address` in `universe`. A pixel never straddles two universes. If
`address` is so close to the end that not one pixel fits, the strip starts at address 1 of the
next universe. The first
universe holds `floor((513 − address) / c)` pixels, where *c* is 3 (RGB) or 4 (RGBW). Every
later universe starts at address 1 and holds `floor(512 / c)`: 170 RGB pixels, or 128 RGBW. The
universes follow on: `universe`, `universe + 1`, …. This is the convention WLED, xLights and
most pixel controllers use. A 300-pixel RGB strip at universe 5 address 1 reads 5 (170) and 6
(130).

Channels in a universe are always **R, G, B (, W)**. `order` is only what the strip wants on
its wire, so a console never has to know it.

### Merging and loss

Per universe, the node keeps up to four sources, keyed by the E1.31 CID. A fifth source
replaces the weakest known one if its priority is higher, and is ignored otherwise.

- **The highest priority present wins.** Sources at the same priority are merged **HTP**,
  slot by slot. This is the console's own input merge (the-pult task 66), so two consoles at
  one priority behave the same in both directions.
- A source is **gone** after 2.5 s of silence (E1.31's network data loss timeout), or at once
  when it sets the *Stream_Terminated* bit. *Preview_Data* packets are ignored.
- The node refreshes the strip once a frame has arrived for every universe that has a source.
  If only some have arrived, it refreshes 8 ms after the first, so a sender that sends the
  universes one after another costs one refresh, not two.

A universe whose sources have all gone keeps its pixels where they were while the strip's other
universes carry on. **When every source of every universe of a strip is gone, the strip holds its last frame
for `hold_ms`, then fades to black over one second.** The reasoning:

- A second console as a hot spare already sends at a lower priority (the-pult's sACN `Auto`
  priority puts each follower below the leader), so a failover hands over inside the 2.5 s
  source timeout with nothing visible.
- The hold covers a switch being power-cycled or a cable reseated: ten seconds of a frozen
  look is better than a blackout in the middle of a scene.
- After that, a strip nobody is driving goes dark rather than staying frozen on a lit look for the
  rest of the night. `hold_ms` 0 holds for ever.

### Rate

A WS2812 pixel takes 30 µs on the wire (24 bits at 800 kHz), and every frame ends with a ≥280 µs
reset. The frame time is `count × 30 µs + 300 µs`. `max_fps` is the lower of 40 and what that
allows: 60 pixels can run at 40, 1024 pixels at 32. A strip in `effect` mode renders at
`max_fps`. In `sacn` mode it refreshes as frames arrive, never faster than `max_fps`. Excess
frames are coalesced, not queued.

### RMT and DMA (V0)

The ESP32-S3 has four RMT transmit channels with 48 symbols of memory each, and **one** of them can
be fed by DMA. Without DMA, the driver refills the channel's memory from an interrupt every 24
symbols, i.e. every pixel. That works, but a burst of wifi interrupts can delay a refill and
glitch the strip.

So **DMA goes to the longer strip** (strip 1 if they are equal), when it is longer than 64 pixels.
The other strip gets two memory blocks (96 symbols), which halves its interrupt rate.

## MQTT

Nothing new. The effect ports are ordinary ports: `openhaunt/<sn>/output/<port>/set` and
`.../effect`, as doc 08 and the effects extension describe. A strip in `sacn` mode has no ports,
and sACN carries its data.

## Bench tool

`firmware/scripts/oh-pixels.py` sends a test pattern to a strip in `sacn` mode (rainbow, chase,
or solid), unicast or multicast, at a chosen priority, and can stop with *Stream_Terminated*. It
also sets a strip's mode over HTTP. See its `--help`.
