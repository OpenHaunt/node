#!/usr/bin/env python3
"""Bench-test a node's audio (OpenHaunt/node doc 13) with no console and no dependencies.

    scripts/oh-audio.py upload 192.168.1.50 scream.wav          # PUT into the clip store, by sha256
    scripts/oh-audio.py list   192.168.1.50
    scripts/oh-audio.py delete 192.168.1.50 <sha>
    scripts/oh-audio.py play   broker:1883 <sn> <sha> --voice ambient --loop --gain 0.5 --in-ms 500
    scripts/oh-audio.py stop   broker:1883 <sn> [--voice ambient] [--fade-ms 500]
    scripts/oh-audio.py gain   broker:1883 <sn> 0.3 [--voice ambient] [--fade-ms 1000]
    scripts/oh-audio.py stream broker:1883 <sn> 192.168.1.50 voice.wav [--delay-ms 150] [--port 5004]
    scripts/oh-audio.py status broker:1883 <sn> [--seconds 30]
    scripts/oh-audio.py clock  broker:1883 [--seconds 600]

Making a clip the node can play:

    ffmpeg -i in.flac -ac 1 -ar 32000 -c:a adpcm_ima_wav clip.wav     # about 16 kB/s
    ffmpeg -i in.flac -ac 1 -ar 48000 -c:a pcm_s16le      clip.wav     # exact, 96 kB/s

Show time: every time on the wire is the console's clock. With no console on the bench,
`--clock` on play, stream and status publishes openhaunt/clock from this machine, so the
node locks to it. Do not do that on a broker a console is publishing on: two clocks
fight. Without it, the node falls back to its own clock, which is SNTP time and usually
within a few milliseconds of this machine's.

`stream` sends 16-bit PCM WAV only, at its own rate and channel count; convert anything
else with ffmpeg first.
"""
import argparse, array, hashlib, http.client, json, os, random, socket, struct, sys, threading, time, wave


# ── A minimal MQTT 3.1.1 client (as scripts/mqtt-sub.py) ────────────────────

def enc_str(s):
    b = s.encode()
    return struct.pack("!H", len(b)) + b


def enc_len(n):
    out = b""
    while True:
        d, n = n % 128, n // 128
        out += bytes([d | (0x80 if n else 0)])
        if not n:
            return out


def packet(kind, body):
    return bytes([kind]) + enc_len(len(body)) + body


def read_packet(sock):
    first = sock.recv(1)
    if not first:
        return None, None
    n, mult = 0, 1
    while True:
        b = sock.recv(1)[0]
        n += (b & 0x7F) * mult
        mult *= 128
        if not b & 0x80:
            break
    body = b""
    while len(body) < n:
        chunk = sock.recv(n - len(body))
        if not chunk:
            break
        body += chunk
    return first[0], body


class Mqtt:
    def __init__(self, broker, client):
        host, _, port = broker.rpartition(":")
        self.sock = socket.create_connection((host or broker, int(port or 1883)), timeout=5)
        self.lock = threading.Lock()
        connect = enc_str("MQTT") + bytes([4, 0x02]) + struct.pack("!H", 30) + enc_str(client)
        self.sock.sendall(packet(0x10, connect))
        kind, body = read_packet(self.sock)
        if kind != 0x20 or body[1] != 0:
            sys.exit("broker refused the connection: %r" % (body,))

    def publish(self, topic, payload, retain=False):
        if not isinstance(payload, (bytes, str)):
            payload = json.dumps(payload, separators=(",", ":"))
        if isinstance(payload, str):
            payload = payload.encode()
        with self.lock:
            self.sock.sendall(packet(0x30 | (0x01 if retain else 0), enc_str(topic) + payload))

    def subscribe(self, topic):
        with self.lock:
            self.sock.sendall(packet(0x82, struct.pack("!H", 1) + enc_str(topic) + bytes([0])))

    def ping(self):
        with self.lock:
            self.sock.sendall(packet(0xC0, b""))


def now_ms():
    return int(time.time() * 1000)


def start_clock(m, stop):
    """Publish openhaunt/clock once a second, retained, the way the console does."""
    def run():
        seq = 1
        while not stop.is_set():
            m.publish("openhaunt/clock", {"t": now_ms(), "seq": seq}, retain=True)
            seq += 1
            stop.wait(1.0)
    t = threading.Thread(target=run, daemon=True)
    t.start()
    return t


# ── HTTP: the clip store ────────────────────────────────────────────────────

def http_req(host, method, path, body=None, headers=None):
    c = http.client.HTTPConnection(host, 80, timeout=120)
    c.request(method, path, body=body, headers=headers or {})
    r = c.getresponse()
    data = r.read()
    return r.status, data.decode("utf-8", "replace")


def cmd_upload(a):
    data = open(a.file, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    t = time.time()
    status, body = http_req(a.host, "PUT", "/api/v1/audio/clips/" + sha, data,
                        {"Content-Type": "audio/wav", "Content-Length": str(len(data))})
    print("%d %s (%d bytes in %.1f s)" % (status, body, len(data), time.time() - t))
    print(sha)
    return 0 if status in (200, 201) else 1


def cmd_list(a):
    status, body = http_req(a.host, "GET", "/api/v1/audio/clips")
    try:
        print(json.dumps(json.loads(body), indent=2))
    except ValueError:
        print(status, body)
    return 0 if status == 200 else 1


def cmd_delete(a):
    status, body = http_req(a.host, "DELETE", "/api/v1/audio/clips/" + a.sha)
    print(status, body)
    return 0 if status == 204 else 1


# ── MQTT: commands ──────────────────────────────────────────────────────────

def with_clock(a, m):
    stop = threading.Event()
    if getattr(a, "clock", False):
        start_clock(m, stop)
        time.sleep(1.2)  # let the node take a sample before anything is timed against it
    return stop


def cmd_play(a):
    m = Mqtt(a.broker, "oh-audio-play")
    stop = with_clock(a, m)
    msg = {"voice": a.voice, "clip": a.sha, "at": now_ms() + a.in_ms, "loop": a.loop, "gain": a.gain}
    if a.fade_in_ms:
        msg["fade_in_ms"] = a.fade_in_ms
    if a.offset_ms:
        msg["offset_ms"] = a.offset_ms
    m.publish("openhaunt/%s/audio/play" % a.sn, msg)
    print("> play", json.dumps(msg))
    time.sleep(0.3)
    stop.set()
    return 0


def cmd_stop(a):
    m = Mqtt(a.broker, "oh-audio-stop")
    msg = {"fade_ms": a.fade_ms}
    if a.voice:
        msg["voice"] = a.voice
    m.publish("openhaunt/%s/audio/stop" % a.sn, msg)
    print("> stop", json.dumps(msg))
    time.sleep(0.3)
    return 0


def cmd_gain(a):
    m = Mqtt(a.broker, "oh-audio-gain")
    msg = {"gain": a.gain, "fade_ms": a.fade_ms}
    if a.voice:
        msg["voice"] = a.voice
    m.publish("openhaunt/%s/audio/gain" % a.sn, msg)
    print("> gain", json.dumps(msg))
    time.sleep(0.3)
    return 0


def cmd_status(a):
    m = Mqtt(a.broker, "oh-audio-status")
    stop = with_clock(a, m)
    m.subscribe("openhaunt/%s/audio/status" % a.sn)
    m.sock.settimeout(2)
    deadline = time.time() + a.seconds
    while time.time() < deadline:
        try:
            kind, body = read_packet(m.sock)
        except socket.timeout:
            m.ping()
            continue
        if kind is None:
            break
        if kind & 0xF0 == 0x30:
            tlen = struct.unpack("!H", body[:2])[0]
            print(time.strftime("%H:%M:%S"), body[2 + tlen:].decode("utf-8", "replace"))
            sys.stdout.flush()
    stop.set()
    return 0


def cmd_clock(a):
    m = Mqtt(a.broker, "oh-audio-clock")
    stop = threading.Event()
    start_clock(m, stop)
    print("publishing openhaunt/clock for %d s, ctrl-c to stop" % a.seconds)
    try:
        time.sleep(a.seconds)
    except KeyboardInterrupt:
        pass
    stop.set()
    return 0


def cmd_stream(a):
    w = wave.open(a.file, "rb")
    if w.getsampwidth() != 2:
        sys.exit("stream sends 16-bit PCM only: ffmpeg -i %s -c:a pcm_s16le out.wav" % a.file)
    rate, ch = w.getframerate(), w.getnchannels()
    frames = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder == "little":
        frames.byteswap()  # L16 is big-endian
    pcm = frames.tobytes()
    per_packet = rate * a.packet_ms // 1000
    bytes_per_packet = per_packet * 2 * ch

    m = Mqtt(a.broker, "oh-audio-stream")
    stop = with_clock(a, m)
    ssrc = random.getrandbits(32)
    ts0 = random.getrandbits(32)
    # The first sample is heard `delay` after now. Packets leave `delay` ahead of the
    # moment they are for, so the node always holds about that much.
    lead = 0.3  # time for the start command to land before the first packet
    at0 = now_ms() + int(lead * 1000) + a.delay_ms
    start = {"op": "start", "port": a.port, "rate": rate, "channels": ch, "ssrc": ssrc, "ts": ts0, "at": at0,
             "gain": a.gain}
    m.publish("openhaunt/%s/audio/stream" % a.sn, start)
    print("> stream", json.dumps(start))
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (a.node, a.port)
    send0 = at0 / 1000.0 - a.delay_ms / 1000.0
    seq = random.getrandbits(16)
    n = 0
    total = len(pcm) // bytes_per_packet
    try:
        while n < total:
            due = send0 + n * a.packet_ms / 1000.0
            wait = due - time.time()
            if wait > 0:
                time.sleep(wait)
            if a.drop_every and n and n % a.drop_every == 0:
                n += 1
                continue  # a lost packet, on purpose
            hdr = struct.pack("!BBHII", 0x80, 96, (seq + n) & 0xFFFF, (ts0 + n * per_packet) & 0xFFFFFFFF, ssrc)
            udp.sendto(hdr + pcm[n * bytes_per_packet:(n + 1) * bytes_per_packet], dest)
            n += 1
            if n % 200 == 0:
                print("  %d/%d packets" % (n, total))
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    m.publish("openhaunt/%s/audio/stream" % a.sn, {"op": "stop", "at": now_ms() + a.delay_ms, "fade_ms": 50})
    print("> stream stop")
    time.sleep(0.3)
    stop.set()
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("upload"); p.add_argument("host"); p.add_argument("file"); p.set_defaults(fn=cmd_upload)
    p = sub.add_parser("list"); p.add_argument("host"); p.set_defaults(fn=cmd_list)
    p = sub.add_parser("delete"); p.add_argument("host"); p.add_argument("sha"); p.set_defaults(fn=cmd_delete)

    p = sub.add_parser("play")
    p.add_argument("broker"); p.add_argument("sn"); p.add_argument("sha")
    p.add_argument("--voice", default="bench")
    p.add_argument("--loop", action="store_true")
    p.add_argument("--gain", type=float, default=1.0)
    p.add_argument("--in-ms", type=int, default=500, help="start this far in the future (negative: in the past)")
    p.add_argument("--fade-in-ms", type=int, default=0)
    p.add_argument("--offset-ms", type=int, default=0)
    p.add_argument("--clock", action="store_true", help="publish openhaunt/clock from this machine")
    p.set_defaults(fn=cmd_play)

    p = sub.add_parser("stop")
    p.add_argument("broker"); p.add_argument("sn")
    p.add_argument("--voice", default="")
    p.add_argument("--fade-ms", type=int, default=0)
    p.set_defaults(fn=cmd_stop)

    p = sub.add_parser("gain")
    p.add_argument("broker"); p.add_argument("sn"); p.add_argument("gain", type=float)
    p.add_argument("--voice", default="")
    p.add_argument("--fade-ms", type=int, default=0)
    p.set_defaults(fn=cmd_gain)

    p = sub.add_parser("stream")
    p.add_argument("broker"); p.add_argument("sn"); p.add_argument("node"); p.add_argument("file")
    p.add_argument("--port", type=int, default=5004)
    p.add_argument("--delay-ms", type=int, default=150)
    p.add_argument("--packet-ms", type=int, default=5)
    p.add_argument("--gain", type=float, default=1.0)
    p.add_argument("--drop-every", type=int, default=0, help="skip every Nth packet, to hear concealment")
    p.add_argument("--clock", action="store_true", help="publish openhaunt/clock from this machine")
    p.set_defaults(fn=cmd_stream)

    p = sub.add_parser("status")
    p.add_argument("broker"); p.add_argument("sn")
    p.add_argument("--seconds", type=float, default=30)
    p.add_argument("--clock", action="store_true")
    p.set_defaults(fn=cmd_status)

    p = sub.add_parser("clock")
    p.add_argument("broker")
    p.add_argument("--seconds", type=int, default=600)
    p.set_defaults(fn=cmd_clock)

    a = ap.parse_args()
    sys.exit(a.fn(a))


if __name__ == "__main__":
    main()
