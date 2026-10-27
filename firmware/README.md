# OpenHaunt Node firmware

Firmware for [OpenHaunt Node](https://github.com/OpenHaunt/node): a PoE-powered,
network-discoverable I/O node for scare mazes, haunted attractions and escape
rooms. One image for every node; the plug-on module — or, on a bare devkit, a
JSON file — decides what the node is.

The protocol is the one in OpenHaunt/node's
[06 — Firmware & Network](https://github.com/OpenHaunt/node/blob/docs/wire-payloads/docs/06-firmware-and-network.md):
mDNS `_openhaunt._tcp`, HTTP `/api/v1`, MQTT topics under `openhaunt/<sn>/`,
sACN for the DMX gateway. It is tested against
[the-pult](https://github.com/RuegerEvents/the-pult), which adopts and drives
nodes over exactly that.

**Ports that animate themselves.** A port may say in `/info` which shapes it can
trace — `"effects": { "shapes": [...], "steps": true, "transitions": true }` — and a
console that sees it sends one descriptor on `openhaunt/<sn>/output/<n>/effect`
instead of a value forty times a second, or a `set` carrying `fade_ms` instead of a
hundred and twenty samples of a fade. Phases are timed against `openhaunt/clock`,
which the console publishes retained once a second for every node on the broker.
A port that advertises nothing is driven exactly as before: absence is the default
and nothing is negotiated. These three additions are ours and are being fed back into
docs/06, the way task 11's assumptions were.

Two things worth knowing about it. A plain `set` always cancels whatever the port was
tracing, which is what makes the protocol robust to reordering: the console stops an
effect with a clear *and* a value, and either order leaves the port in the right
place. And a node upgraded in the field keeps the module description it had stored,
so it does not start advertising until the module is re-applied — `oh vmod preset
<key>`, or a fresh EEPROM read.

## Boards

| `BOARD` | Hardware | Transport | Status |
|---|---|---|---|
| `oh-base` | The OpenHaunt base board: ESP32-WROOM-32, RMII Ethernet (RTL8201), W25Q128 | Ethernet, Wi-Fi fallback | compiles; no hardware yet |
| `esp32c6-devkitc-1` | ESP32-C6-DevKitC-1 | Wi-Fi | compiles; untested |
| `esp32-1732s019` | Sunton ESP32-1732S019 (ESP32-S3, 1.9" ST7789) | Wi-Fi | tested end to end |
| `oh-node-v0.1` | OpenHaunt Node V0.1 prototype (ESP32-S3-WROOM-1-N16R2, W5500, 12 V PoE) | Ethernet, Wi-Fi fallback | builds; not yet flashed |
| `oh-node-v0.2` | OpenHaunt Node V0.2 prototype (same, 5 V PoE or USB power bank, line out) | Ethernet, Wi-Fi fallback | builds; not yet flashed |

Everything board-specific lives in `boards/<board>/sdkconfig.board` and the
Kconfig menu *OpenHaunt board* (`components/oh_board/Kconfig`): pins, transport,
status LED, display, the GPIOs presets may bind.

## The V0 prototype

The V0 boards (OpenHaunt/node `hardware/OH_Node_V0_Prototype`, pin plan in docs/12) carry
their I/O on the board and have no module connector, so there is no EEPROM to read. A V0
node describes itself from the board instead: module type **`0x0F01` "Node V0"**, source
`board`, rev `0.1` or `0.2`, with up to 24 ports and two **heads** (OpenHaunt/node doc 14):

| Port | What | Head | Driver | Pin (V0.1 / V0.2) |
|---:|---|---:|---|---|
| 0–3 | Relay 1–4, class `switch` | 0 | `gpio_out` | 4, 5, 6, 7 |
| 4–5 | Sensor 1–2, class `contact`, sensor 1 the lower terminal | 0 | `gpio_in` | **16, 15** / 15, 16 |
| 6–14 | Strip 1 in `effect` mode: Color, Brightness, Color 2, Effect, Speed, Size, Reverse, Mirror, Segments | 1 | `ws2812` | 17 |
| 15–23 | Strip 2, the same nine | 2 | `ws2812` | 18 / **38** |

A strip in `sacn` mode keeps its head and owns no ports; its universes carry it. The ports and
their names are the same on both revisions, so the console derives one fixture type per head
for all ten nodes. Only the bindings differ. A stored virtual module still wins over this
(`oh vmod set ...`), and `oh vmod clear` goes back to it.

Decisions, and why:

- **A sensor port reads true for a contact closed to GND, on both revisions.** V0.1 pulls the
  GPIO up through the terminal, so a closed contact reads low; V0.2's transistor stage reads
  it high. The board config (`OH_SENSOR_CONTACT_READS_LOW`) inverts to match, so the port
  means the same on either. An open terminal reads false on both. A sensor that *drives* its
  output (an HC-SR501 style 3.3 V "high on motion") reads the other way round on both
  revisions. Flip `invert` on its binding with a virtual module for that one.
- **Each strip is a head**, so both carry the `color` and `intensity` classes without landing
  on one parameter key: a class is unique per head. That needs a head-aware console.
- **60 pixels per strip by default, and DMA to the longer one.** The S3 has one DMA-capable
  RMT channel; the board gives it to the longer strip above 64 pixels and the other two memory
  blocks. `OH_STRIP_PIXELS` is the default count; `PUT /api/v1/pixels/<n>` sets each strip's.
- **Ethernet first, and Wi-Fi standing by beside it** (OpenHaunt/node D38). With Wi-Fi
  credentials both start at boot and both stay up; the cable is preferred while its link is
  up (checked every 100 ms) and Wi-Fi carries everything the moment it is not. The node is up
  while either path is, the MQTT connection is bound to the preferred path and moved when it
  changes, and retained `openhaunt/<sn>/net` lists both addresses so a console can move a
  stream at once or send it down both. Without credentials Wi-Fi is still the fallback, and
  starts to open the setup AP if the cable has produced no address within
  `OH_WIFI_STA_TIMEOUT_S` (30 s), if the W5500 does not answer on SPI (after 1 s), or 30 s
  after the link goes down later.
- **The W5500 is given the S3's Ethernet MAC** from efuse, since it has none of its own.
  It runs at 20 MHz (`OH_W5500_CLOCK_MHZ`) because the V0 pins go through the GPIO matrix.
- **Yellow is status, red is faults.** The yellow LED (GPIO36) shows the usual patterns; on a
  board with a red LED (GPIO35) a fault double-flashes that instead of riding on the yellow.
  On V0.2 the LED beside each relay and each sensor terminal follows the pin itself, whichever
  module is bound.
- **Audio is the `oh_audio` component** (below). Boot holds the amplifier off and the DAC
  muted, as before, until it starts. V0.2's line out is enabled for good from then on; the
  speaker amplifier is powered only while something is playing.
- **BOOT toggles dark mode.** A press (GPIO0, debounced 100 ms) turns every indicator
  LED off: status, error, and on V0.2 the relay and sensor LEDs. Identify still blinks.
  Dark is kept in NVS, so a node that reboots mid-show stays dark, and is also set with
  `POST /api/v1/config {"dark": true}` or the web UI. `/status` shows `dark` and the
  button (`buttonPressed`), live on the event stream. The Ethernet jack's LEDs are the
  W5500's own and stay on. A virtual module that binds GPIO0 as a contact still gets the
  presses, and each one still toggles dark.
- **The console is the USB-C socket** (the S3's USB Serial/JTAG). UART0 is on test points.

## Pixel strips

Each of a V0's two strip outputs is in one of two modes, set with `PUT /api/v1/pixels/<0|1>`
or in the web UI (Node tab, Strips: pixel count, color order, mode, universe) and kept in NVS (OpenHaunt/node [14 — Pixel strips, and heads](https://github.com/OpenHaunt/node/blob/main/docs/14-pixels.md)):

- **`effect`**: the node draws every pixel from nine ports. The effects are `solid`, `gradient`,
  `chase`, `comet`, `wave`, `breathe`, `fire`, `candle`, `sparkle` and `lightning`, each worked
  out from the console's clock, so two strips agree without talking to each other.
- **`sacn`**: the strip maps universes onto its pixels: up to 1024 pixels, spilling 170 RGB (128
  RGBW) a universe, any color order. It merges up to four sources per universe (highest
  priority wins, HTP between equals, 2.5 s source timeout), and when every source is gone it
  holds the last frame for `hold_ms` (10 s) and then fades out over a second.

| | |
|---|---|
| Pure logic | `oh_pixel` (settings, mapping, rate, DMA, loss), `oh_pixfx` (effects), `oh_sacn_merge`, host-tested |
| Driver | `ws2812`: a task per strip, RMT through `led_strip`, a frame only when it differs from the last |
| Receiver | `oh_sacn`: one socket on 5568 for the gateway and every strip, five multicast joins at most |
| Rate | `count × 30 µs + 300 µs` a frame, capped at 40 fps: 40 at 60 pixels, 32 at 1024 |
| Telling the console | mDNS TXT `desc`, a checksum over the description, changes when a strip changes mode |

Decisions, and why:

- **The mode is chosen at patch time and stored**, not switched per cue. A strip that switched
  between the console drawing it and the node drawing it would have two owners mid-scene.
- **Effect parameters are ordinary ports**, so the console's fades, and the node's own shapes
  and transitions, work on Speed or Color exactly as they do on a dimmer. Effect is a string
  port, because a number would be a list index the console has to keep in step.
- **Deterministic randomness.** `fire`, `candle`, `sparkle` and `lightning` are a hash of the
  console time and the pixel, not a generator with state, so a restart or a second node draws
  the same frame. The phase effects integrate Speed (so a speed fade never jumps) and are pulled
  2% a frame towards `time × speed` (so two strips lock together).
- **Hold, then dark.** A hot spare already sends at a lower priority and takes over inside the
  2.5 s timeout, so the hold is for a switch rebooting. After it a strip nobody drives goes
  dark rather than freezing on a lit look all night. `hold_ms` 0 holds for ever.
- **No gamma on the node.** What arrives is what the strip gets, in both modes.
- **`OH_MAX_PORTS` went from 16 to 32**, for the 24 ports. The effect and transition tables
  are static and sized by it: about 25 kB more BSS. DIRAM on V0.2 is at 59%.

`scripts/oh-pixels.py` sets a strip's mode and sends test patterns (rainbow, chase, solid) by
unicast or multicast, at a priority, optionally ending with Stream_Terminated.

## Audio

A V0 node plays **clips stored on its flash** and **an RTP stream**, mixed, each started at a
**show time** on the console's clock, so a skeleton's voice on the node's speaker lands with its
jaw on a relay. The protocol is OpenHaunt/node [13 — Audio](https://github.com/OpenHaunt/node/blob/main/docs/13-audio.md):
an `audio` object in `/info`, `PUT|GET|DELETE /api/v1/audio/clips[/<sha256>]`, and
`openhaunt/<sn>/audio/{play,stop,gain,output,stream}` with `audio/status` back.

| | |
|---|---|
| Mix | 48 kHz stereo, 4 clip voices plus the stream, 32-bit sum clipped to 16 |
| Clips | RIFF WAVE, PCM16 or IMA ADPCM, mono/stereo, any rate (resampled), on FAT partition `clips` (9.8 MiB) |
| Stream | RTP L16, any rate, 1 s buffer, played by its mapping to show time |
| V0.1 | MAX98357A only, wired to one I2S slot, so the mix is **downmixed to mono** |
| V0.2 | PCM5102A stereo line out (`XSMT` GPIO39) and the MAX98357A on the same bus, mixing (L+R)/2 itself |

Decisions, and why:

- **WAV with IMA ADPCM, not MP3.** Same density as 128 kbit/s MP3 (about ten minutes of 32 kHz mono
  in the store), a table lookup per sample to decode, about a hundred lines with host tests, no
  license to carry, and blocks that make a seek exact. ESP-IDF ships no MP3 decoder.
- **Every sample is placed by its show time.** The I2S `on_sent` interrupt timestamps each DMA
  buffer leaving for the DAC, so the task knows when the next frame it writes will be heard. The
  mixer is told that time and nothing else, which is what makes it a pure function the host tests
  can drive. Latency in the output buffer then costs nothing, so the ring is six 10 ms buffers:
  enough to ride out a flash erase.
- **A late command joins where the clip would be by now** instead of starting late from the
  beginning. That rule keeps a voice in step with everything else started at the same `at`.
- **The console chooses the stream's delay; the node only follows the mapping.** The node plays
  RTP timestamp `ts` at show time `at` and nothing earlier. Drift between the two crystals is
  trimmed by ±0.1% between 1 and 20 ms apart, and anything further is a jump, counted as `resyncs`.
- **A lost packet is a fade, not a stall.** The last sample decays over about 20 ms, and the rest
  of the stream stays where it belongs in time.
- **Not a port.** Sound has no value to set, and an unknown `dataType` would make every console that
  predates it reject the node. The `audio` object sits beside the ports, and if it is absent the
  node has no audio, as with effects.
- **`littlefs` became `clips`.** The 16 MB table's data partition was declared and never mounted.
  It is FAT with wear leveling now, because ESP-IDF carries both in-tree and long file names
  (`<sha256>.wav`) need nothing more than `FATFS_LFN_HEAP`. **Changing the partition table needs a
  USB flash**: OTA cannot do it.

Traps worth knowing:

- **Uploading while playing can underrun.** Writing flash stops the caches for a sector erase. The
  60 ms ring covers one, but not a long upload during a scare. Upload before the show.
- **V0.1's jack is the bridged amplifier output**, not a line out. Never plug it into a mixer.
- **Gains add up.** Four voices at 1.0 clip. Setting levels is the console's job.
- **Show time steps once, when SNTP first sets the clock.** Schedule against the console's clock
  (`audio/status` says `"locked": true` once the node has it).

`scripts/oh-audio.py` uploads, plays, stops and streams from a laptop with no dependencies, and
can stand in for the console's clock on a bench.

## Brokers, and a console that fails over

A node is told an **ordered list of brokers**, leader first: every station of the console's
session (OpenHaunt/node [15 — Brokers](https://github.com/OpenHaunt/node/blob/main/docs/15-brokers.md)).
It comes as `mqtt.brokers` in `POST /api/v1/config`, and as the retained
`openhaunt/brokers` the leader publishes, which a node subscribes to on every connection.
It is kept in NVS (`mqtt_brokers`, comma-joined, beside the old `mqtt_broker`), so a node
that reboots while its leader is dead still knows where the others are.

Losing the broker it is on, a node tries the next entry 200 ms later, and once a whole
pass has failed it waits 1 s, doubling to 8 s. A new list whose first entry is not the
broker it is on moves it there at once. Only the leading station's broker accepts a node,
so the walk ends where the show is being run from, and that leader's list then puts itself
first. Input events from the gap are kept, 32 of them in order, and flushed on connecting.

`oh_brokers` in `oh_core` is the walk and the parsing, with host tests; `oh_mqtt` drives
esp-mqtt with auto-reconnect off and points the client at the next entry itself. A console
from before lists sends `mqtt.broker` alone, which is a list of one.

## Building

ESP-IDF v5.5 (installed with [eim](https://github.com/espressif/idf-im-ui) at
`~/.espressif/v5.5.3`; `scripts/env.sh` puts it in the shell).

```
scripts/build.sh esp32-1732s019                       # build
scripts/build.sh esp32-1732s019 -p /dev/cu.usbserial-110 flash monitor
scripts/build.sh oh-node-v0.2 -p /dev/cu.usbmodem101 flash monitor   # USB-C on the board
scripts/build.sh oh-base menuconfig
scripts/build.sh esp32c6-devkitc-1 --fresh build      # after changing sdkconfig defaults
```

Each board gets its own `build/<board>/`. The pure-C core has host tests:

```
cmake -S test/host -B build/host && cmake --build build/host && ctest --test-dir build/host
```

## First boot

With no Wi-Fi credentials (or when the configured network cannot be reached
for 30 s) the node opens an access point **`openhaunt-<sn>`** and a captive
portal at `http://192.168.4.1/`. Enter the network there, or on the console:

```
oh wifi set <ssid> <passphrase>
```

Once on the network the node is `openhaunt-<sn>.local` with a web UI at `/`
(status, network, name and broker, strips, module editor, firmware update) and the
`/api/v1` the controller uses.
The UI follows the node live over `GET /api/v1/events`, a server-sent event
stream: `status` (what `/status` answers) on connect, on any network, broker,
error or config change and every 5 s; `ports` (the `ports` array) within 50 ms
of a port changing, so a sensor shows the moment it trips; `module` when the
node binds its I/O again. Two streams at a time, a third evicts the oldest. A
node without the stream gets polled every 2 s as before.

## What the node is

The module is chosen at boot, in this order:

1. **The ID EEPROM** at I2C 0x50 (the base board's module connector), per doc 04.
   The module is held in reset until its descriptor is read, validated, and
   found to be within the power budget.
2. **A virtual module** stored in NVS: a JSON description in the same shape as
   the-pult's node-sim config, plus `bindings` that tie ports to drivers and pins.
   This is how a devkit poses as any module, and how a module nobody has built
   yet runs.
3. Nothing: headless, on the network, findable, reporting `module-absent`.

Catalog presets (`input`, `contact`, `env`, `relay`, `led`, `oled`, `dmx`) are
one click in the web UI or `oh vmod preset <key>` on the console. A virtual
module with a real button and a real switch on a devkit:

```json
{ "name": "Bench node", "module": { "type": "0x0006", "name": "Dry Contacts" },
  "ports": [ { "port": 0, "name": "Contact 1", "access": "readwrite", "dataType": "boolean", "default": 0, "class": "switch" },
             { "port": 1, "name": "Button", "access": "readonly", "dataType": "boolean", "class": "contact" } ],
  "bindings": [ { "driver": "gpio_out", "port": 0, "gpio": 18 },
                { "driver": "gpio_in", "port": 1, "gpio": 9, "pull": "up", "invert": true } ] }
```

A port with no binding is a software port: it holds whatever it is given, which
is what the simulator does and what lets any preset run on a bare board.

### Drivers

| id | ports | binding keys |
|---|---|---|
| `gpio_in` | readonly boolean | `gpio`, `pull` (`up`/`down`/`none`), `invert`, `debounce_ms` |
| `gpio_out` | readwrite boolean | `gpio`, `invert`, `open_drain` |
| `ws2812` | color + number (intensity) | `gpio`, `count`, `order` |
| `mcp23017_in` | up to 16 readonly boolean | `addr`, `int_gpio`, `pins`, `invert`, `pullup` |
| `mcp23017_out` | up to 16 readwrite boolean | `addr`, `pins`, `invert` |
| `sht4x` | temperature + humidity (readonly number) | `addr`, `interval_ms`, `hyst_c`, `hyst_rh` |
| `ssd1306` | readwrite string | `addr`, `height` |
| `dmx_uart` | none; the universe is the interface | `uart`, `tx`, `rx`, `de`, `refresh_hz` |

Inputs are interrupt-driven and timestamped in the ISR; the timestamp on the
wire is unix milliseconds once SNTP has synced and milliseconds since boot before.

## Layout

```
components/
  oh_core      pure C, no IDF: port/module model, EEPROM descriptor, every wire
               shape (info/state/payloads/input/health/audio), virtual-module parser,
               presets, debounce, E1.31 parser, WAV/ADPCM, RTP, the jitter buffer
               and the audio mixer                    <- host tests cover this
  oh_board     Kconfig board choice and the pin table
  oh_node      state store, driver registry, module bind, event fan-out, errors
  oh_drivers   the drivers above; oh_i2c the shared bus; oh_font Spleen 8x16
  oh_config    NVS: name, broker, dmx, Wi-Fi, IP; partial /config merge
  oh_net       Ethernet or Wi-Fi, setup AP + captive DNS, mDNS, SNTP
  oh_http      /api/v1 and the embedded web UI (www/index.html)
  oh_mqtt      status/input/health/output topics, offline will, event backlog
  oh_sacn      E1.31 receiver for a node that forwards a universe
  oh_ota       firmware update into the other slot, with rollback
  oh_audio     I2S out, the clip store on FAT, the RTP receiver, the output task;
               the WAV reader, mixer, jitter buffer and wire shapes are in oh_core
  oh_status    status LED patterns and identify
  oh_display   ST7789 status screen (boards with one)
  oh_console   the `oh` REPL
  oh_module_eeprom  24AA02UID descriptor, MOD_nRESET, the catalog table
  oh_app       ties it together
main/app_main.c   the boot sequence
```

## Console

`oh help` on the serial console (UART0, 115200). `scripts/oh-serial.py` drives it
without a terminal, and `scripts/oh-cmd.sh` sends lines to a running
`--daemon` session:

```
scripts/oh-serial.py /dev/cu.usbserial-110 --daemon build/serial &
scripts/oh-cmd.sh "oh info" "oh vmod preset input" "oh in 1 1"
```

`scripts/mqtt-sub.py` watches a broker with no dependencies, `scripts/oh-audio.py` drives a
node's audio (see *Audio*), and
`scripts/pult-call.mjs` adopts or identifies a node in a running the-pult.

## Status

**On a V0.2 (2026-10-26), against the-pult:** Ethernet and Wi-Fi, moving between them both
ways with PoE and USB plugged in together (no reset, no gap with a console streaming over
Wi-Fi), adoption and MQTT, the broker list across a console restart, the relays and their
LEDs, the status LEDs, the line out in stereo (left is left), clips from the store and an RTP
stream with no drops over the cable, show time locked to the console, OTA, and a core dump
read back over HTTP. Not yet: the sensor terminals, both strips, the speaker, a redundant
stream (the console side is being written), and the V0.1.

Two things worth knowing when a node misbehaves: `/status` says `resetReason` and whether a
`coredump` is in flash, and `GET /api/v1/coredump` returns it raw for
`esp-coredump info_corefile --core-format raw`, against the ELF of the image that crashed
(`DELETE` to clear it).

Still written only from the schematic, the datasheets and the ESP-IDF documentation: the
sensor polarity on real terminals, the strips on GPIO17/18/38 (both modes, the DMA split, RGBW,
the sACN receiver with several multicast joins, the frame rates), the speaker amplifier, and
everything on the V0.1.

Verified on the ESP32-1732S019 against the-pult: setup AP and captive portal,
discovery and the TXT record, every `/api/v1` endpoint, adoption, MQTT in both
directions with the will, OTA with rollback. The base board's Ethernet, the ID
EEPROM path and the I2C drivers compile but have not met their hardware; the
DMX gateway listens for sACN and drives the UART but has not met a fixture.
