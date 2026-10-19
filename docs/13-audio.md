# 13 — Audio

A node can make sound. It plays **clips stored on the node** and **a stream sent to it over
RTP**, and it mixes both. Every sound starts at a stated **show time**, so a skeleton's jaw on
a relay and its voice on the node's speaker land together, even over wifi.

This is an extension to doc 08, in the same spirit as the effects extension: a node that
advertises nothing about audio is driven exactly as before, and nothing is negotiated.

## What a node says it has

`GET /api/v1/info` gains a top-level `audio` object when the node has an audio output. If the
object is absent, the node has no audio.

```json
"audio": {
  "rate": 48000,
  "voices": 4,
  "outputs": [
    { "id": "line",    "name": "Line out", "kind": "line", "channels": 2 },
    { "id": "speaker", "name": "Speaker",  "kind": "amp",  "channels": 1 }
  ],
  "clips":  { "container": "wav", "formats": ["pcm16", "ima-adpcm"],
              "bytes_total": 10264576, "bytes_free": 9871360 },
  "stream": { "payload": "L16", "max_buffer_ms": 1000 }
}
```

- `rate` is the rate the node mixes and plays at. Clips and streams at any other rate are
  resampled (linear interpolation), so a console does not have to match it.
- `voices` is how many clips can play at once. The stream is a voice of its own, on top of these.
- `outputs` names what the node's sound comes out of. Every output plays the **same mix**: there is
  one I2S bus, and neither chip has a volume control of its own. What varies is whether an output
  is enabled.
  - **V0.1** has one output: `speaker` (`amp`, 1 channel). Its 3.5 mm jack carries the
    bridged amplifier output. **It is not a line out, and must never be plugged into a line
    input.** The amplifier is wired to play one I2S slot, so the node downmixes the whole mix
    to mono before sending it.
  - **V0.2** has `line` (PCM5102A, stereo, 3.5 mm) and `speaker` (MAX98357A on a screw terminal,
    playing the (L+R)/2 mix).
- `clips.bytes_total` and `bytes_free` are the clip store's budget, also reported by `GET
  /api/v1/audio/clips`.

Why not a port: a port is a terminal with a value (doc 08, E1.73 UDR). Sound has no value to
set or read, and a new `dataType` would make every console that predates it reject the whole
node. The audio object sits beside the ports instead.

## Stored clips

### Format

A clip is a **RIFF WAVE file**, either **16-bit PCM** (format tag 1) or **IMA ADPCM** (format tag
0x11, the variant `ffmpeg -c:a adpcm_ima_wav` writes), mono or stereo, at any sample rate.

Why WAV with IMA ADPCM, and not MP3:

- **Flash.** IMA ADPCM is 4 bits per sample: 32 kHz mono is 16 kB/s, so the 10 MB store holds about
  ten minutes. That is the same density as 128 kbit/s MP3. PCM16 is there for the short, sharp clips
  where ADPCM's noise floor would show.
- **CPU.** An ADPCM decoder is a table lookup and a shift per sample. MP3 costs a few percent of a
  core per voice, times four voices plus a stream, on a core that also runs the network.
- **Licence and size.** ESP-IDF ships no MP3 decoder. The usable ones are either a component from
  Espressif's registry under its own licence, or a vendored library of thousands of lines. The
  ADPCM decoder here is about a hundred lines and has host tests.
- **Seeking is exact.** ADPCM is coded in independent blocks, so the node can start a clip part
  way through (see *late starts*) without decoding from the beginning.

Making one:

```
ffmpeg -i scream.flac -ac 1 -ar 32000 -c:a adpcm_ima_wav scream.wav       # ADPCM
ffmpeg -i bang.flac   -ac 1 -ar 48000 -c:a pcm_s16le    bang.wav          # PCM16
```

### The store

The clips live on a FAT partition (`clips`, wear-levelled) in the 16 MB flash, after both
3 MB OTA slots. On the V0 boards that is 0x9D0000 bytes, about 9.8 MiB. Each clip is a file named
by the **sha256 of its bytes**, so a clip is uploaded once and named the same way by every node
and the console.

```
PUT    /api/v1/audio/clips/<sha256>    the WAV, as the request body
GET    /api/v1/audio/clips             what is stored, and the budget
DELETE /api/v1/audio/clips/<sha256>
```

- **PUT** streams the body to the store while hashing it. It answers `201` when the bytes hash to
  the sha in the URL and the file is a WAV the node can play. It answers `200` if the clip is
  already stored (nothing is written), `400` on a hash mismatch or a WAV it cannot play (the
  partial file is removed), `413` if it does not fit (checked against `Content-Length` before
  anything is written), and `409` if another upload is running.
- **GET** answers
  `{"clips":[{"sha":"…","bytes":51244,"format":"ima-adpcm","rate":32000,"channels":1,"duration_ms":3120}],"bytes_total":…,"bytes_free":…}`.
- **DELETE** answers `204`, `404` if the clip isn't stored, or `409` while a voice is playing it.

**Upload clips before the show, not during it.** Writing flash stops the processor's caches for
the length of a sector erase, which is tens of milliseconds. The output buffer covers about 60 ms
(see *Timing*), so one erase does not interrupt the sound, but a long upload during playback can
cause an underrun, and the node counts it.

### Commands

On MQTT, beside `output/<n>/set`:

```
openhaunt/<sn>/audio/play     start a clip on a voice
openhaunt/<sn>/audio/stop     stop one voice, or all of them
openhaunt/<sn>/audio/gain     change a voice's gain, or the master gain
openhaunt/<sn>/audio/output   enable or disable one output
openhaunt/<sn>/audio/stream   start, re-anchor or stop the RTP stream
```

Every time below is **show time** in milliseconds: the console's clock as `openhaunt/clock` carries
it. A missing `at` means *now*.

**play**

```json
{ "voice": "ambient", "clip": "3f9a…", "at": 1761830400123,
  "loop": true, "gain": 0.6, "fade_in_ms": 2000, "offset_ms": 0 }
```

- `voice` is a name chosen by the console, up to 15 characters. Playing on a voice that is already
  playing replaces it, with a 10 ms crossfade, so "the scare on area 3" is always one voice whatever
  happened before. Different names play at the same time, so an ambient loop and a scare overlap.
  A fifth name while four are playing takes over the voice that started earliest.
- `clip` is the sha256 of a stored clip. An unknown clip is refused and logged, and nothing plays.
- `loop` repeats the clip without a gap until it is stopped.
- `gain` is a linear amplitude factor from 0 to 1. The default is 1.
- `fade_in_ms` ramps from silence to `gain`.
- `offset_ms` starts that far into the clip.

**Late starts.** If `at` has already passed when the command arrives, the node does not start the
clip from the beginning late. It starts **where the clip would be by now**, so that it stays in
time with everything else the console started at `at`. A non-looping clip whose end has already
passed does not play. This is the rule that keeps a voice on a jaw.

**stop**

```json
{ "voice": "scare", "at": 1761830403000, "fade_ms": 500 }
```

No `voice` means every voice and the stream. `fade_ms` ramps to silence, and the voice ends when the
ramp does. The default is a 10 ms ramp, which is fast but does not click.

**gain**

```json
{ "voice": "ambient", "gain": 0.3, "fade_ms": 1500 }
```

No `voice` sets the master gain (default 1), which scales the whole mix. Master gain is the only
volume control the V0 boards have. Gain changes always ramp, over at least 10 ms.

**output**

```json
{ "output": "speaker", "enabled": false }
```

Both outputs are enabled when the node boots. The `speaker` amplifier is powered only while
something is playing, so a disabled-or-idle speaker draws nothing. This matters on V0.2, whose
PoE budget is 8.9 W of 9 W at worst.

### Status

`openhaunt/<sn>/audio/status` (not retained) is published once a second while anything is playing,
and once more when the node goes quiet:

```json
{ "voices": [ { "voice": "ambient", "clip": "3f9a…", "pos_ms": 81234, "loop": true, "gain": 0.6 } ],
  "stream": { "active": true, "port": 5004, "depth_ms": 148, "late": 0, "missing": 12, "resyncs": 0 },
  "underruns": 0, "master": 1.0, "locked": true }
```

- `locked` says whether the node has the console's clock yet (see `openhaunt/clock`). Before it
  does, show time is the node's own clock, and anything scheduled against the console is off by
  however far apart the two clocks are.
- `underruns` counts output blocks the mixer could not produce in time. It should stay at 0.
- `depth_ms` is how much of the stream is buffered ahead of what is playing. `late` counts packets
  that arrived after their sound had already played. `missing` counts frames that never arrived.
  `resyncs` counts hard re-alignments (see *Drift*).

## The RTP stream

### Payload

**L16** (RFC 3551: 16-bit signed, big-endian, interleaved if stereo), at any rate, mono or stereo,
over UDP. One stream per node. The payload type is not checked: the session says what the bytes
are.

Why L16 and not Opus: one channel of 48 kHz L16 is 768 kbit/s, which a 100 Mbit link and ordinary
wifi carry without effort. Decoding it costs nothing, and a lost packet is a hole in the sound
rather than a decoder that has lost its state. Opus would make streaming to many nodes over weak
wifi possible, but at a large firmware cost. That is left for later.

### Session

```json
{ "op": "start", "port": 5004, "rate": 48000, "channels": 1,
  "ssrc": 305419896, "ts": 1000, "at": 1761830400123, "gain": 1.0 }
```

- `ts` and `at` are the mapping: **RTP timestamp `ts` is heard at show time `at`**. Every other
  sample follows at `rate`. The node does nothing else with RTP timestamps, so the console is free
  to start them anywhere.
- `ssrc` is optional. When given, packets from any other source are dropped.
- Send `start` **before** the first packet. The node opens the UDP port within 100 ms of the
  command, and anything that arrives before that is lost.
- Sending `start` again on the same `port` **re-anchors** the mapping without dropping what is
  already buffered. The console does this after a locate.
- `{"op":"stop","at":…,"fade_ms":…}` ends the session. Stop and gain for the stream also go through
  `audio/stop` and `audio/gain` with `"voice":"stream"`.

### Timing

**The console chooses the delay.** The node plays every sample at the show time the mapping gives
it, and nothing earlier. So the console sends audio ahead of the moment it should be heard, by the
playout delay, and compensates on its own side by mapping that audio to `at = now + delay`.

- The default the console should use is **150 ms**. That rides out ordinary wifi jitter and still
  feels immediate for a triggered scare.
- The node buffers up to `max_buffer_ms` (1000 ms). Anything further ahead is dropped and counted as
  `late`.
- The output buffer itself is about 60 ms. The node measures the time at which each output buffer
  actually leaves for the DAC, so the mixer knows the show time of every sample it writes, to within
  a buffer of 5 ms or so.

### Loss and drift

- **A packet that never arrives is silence, not a stall.** The mixer never waits for the stream.
  The gap is concealed by holding the last sample and fading it out over about 20 ms, so there is
  no click. The rest of the stream stays where it belongs in time.
- **A packet that arrives too late is dropped** and counted as `late`.
- **Drift.** The console's sample clock and the node's crystal are not the same crystal, and over an
  evening they part by a fraction of a second. The node never lets that build up. On every output
  block it compares where the stream *is* with where the mapping says it *should be*:
  - Under 1 ms apart, it does nothing.
  - Up to 20 ms, it speeds the stream up or slows it down by 0.1% until the two agree. That is
    inaudible, and it closes 1 ms per second.
  - Beyond 20 ms (a stepped clock, a re-anchor), it jumps straight to the right place and counts a
    `resync`.

### Mixing

The stream is one more voice in the mix, at its own gain, under the master gain, beside the clips.
The mix is summed in 32 bits and clipped to 16. **Gains add up**: four voices at 1.0 can clip, and
it is the console's job to set gains so they don't.

## Show time on the node

Show time is the node's estimate of the console's clock (`oh_clock_console_now_*`). It carries over
the same caveats the effects extension already has:

- Before the first `openhaunt/clock` message arrives, show time is the node's own clock. Status says
  so with `"locked": false`.
- The estimate moves by at most 50 ms per sample. A clip that is already playing just carries on,
  but the stream re-aligns to a moved clock under the drift rule above.
- When SNTP first sets the node's wall clock, the node's own time steps from milliseconds-since-boot
  to unix time. Anything scheduled before that against the node's own clock is meaningless.
  Schedule against the console's clock.

## Bench testing without a console

`scripts/oh-audio.py` in the firmware repository uploads clips, plays them, and sends an RTP stream
from a WAV, all with no dependencies. It can also publish `openhaunt/clock` itself, so a node on the
bench locks to the laptop.
