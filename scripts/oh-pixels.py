#!/usr/bin/env python3
"""Bench-test a node's strips (OpenHaunt/node doc 14) with no console and no dependencies.

    scripts/oh-pixels.py show   192.168.1.50                         # GET /api/v1/pixels
    scripts/oh-pixels.py mode   192.168.1.50 1 sacn --universe 5 --count 300
    scripts/oh-pixels.py mode   192.168.1.50 0 effect
    scripts/oh-pixels.py send   rainbow --universe 5 --count 300 --to 192.168.1.50
    scripts/oh-pixels.py send   chase   --universe 5 --count 300 --priority 120
    scripts/oh-pixels.py send   solid   --universe 5 --count 300 --rgb 255,40,0 --seconds 5 --terminate

`send` with no --to sends multicast (239.255.hi.lo), the way a console does by default;
--to sends unicast to the node, which reaches any number of universes (multicast joins are
limited to four on a node). Channels go out R, G, B (, W with --rgbw) whatever the strip's
own colour order is: the node reorders.

`--terminate` ends with three Stream_Terminated packets, so the node drops this source at
once rather than after 2.5 s; without it, stopping the script is a source going silent, and
the strip holds its last frame for its hold_ms and then fades out.
"""
import argparse, colorsys, http.client, json, math, socket, struct, sys, time, uuid

PORT = 5568


def http_req(host, method, path, body=None):
    c = http.client.HTTPConnection(host, 80, timeout=10)
    data = json.dumps(body).encode() if body is not None else None
    c.request(method, path, data, {"content-type": "application/json"} if data else {})
    r = c.getresponse()
    text = r.read().decode()
    return r.status, text


def cmd_show(a):
    status, text = http_req(a.host, "GET", "/api/v1/pixels")
    print(status)
    try:
        print(json.dumps(json.loads(text), indent=2))
    except ValueError:
        print(text)


def cmd_mode(a):
    body = {"mode": a.mode}
    for key in ("count", "order", "universe", "address", "hold_ms"):
        v = getattr(a, key)
        if v is not None:
            body[key] = v
    status, text = http_req(a.host, "PUT", "/api/v1/pixels/%d" % a.output, body)
    print(status, text)


def e131_packet(cid, universe, seq, priority, slots, options=0, name="oh-pixels"):
    """One E1.31 data packet (ANSI E1.31-2018 §4-6)."""
    n = len(slots)
    dmp = struct.pack("!HBBHHH", 0x7000 | (10 + 1 + n), 0x02, 0xA1, 0x0000, 0x0001, n + 1) + b"\x00" + bytes(slots)
    framing = (struct.pack("!HI", 0x7000 | (77 + len(dmp)), 0x00000002)
               + name.encode().ljust(64, b"\x00")[:64]
               + struct.pack("!BHBBH", priority, 0, seq & 0xFF, options, universe) + dmp)
    root = (struct.pack("!HH", 0x0010, 0x0000) + b"ASC-E1.17\x00\x00\x00"
            + struct.pack("!HI", 0x7000 | (22 + len(framing)), 0x00000004) + cid + framing)
    return root


def frame(pattern, count, t, rgbw, rgb):
    """Pixels as (r, g, b[, w]) tuples for time t."""
    out = []
    for i in range(count):
        if pattern == "rainbow":
            r, g, b = colorsys.hsv_to_rgb((i / count + t * 0.2) % 1.0, 1.0, 1.0)
            px = (int(r * 255), int(g * 255), int(b * 255))
        elif pattern == "chase":
            lit = (i - int(t * count / 2)) % count < max(1, count // 10)
            px = rgb if lit else (0, 0, 0)
        else:
            px = rgb
        out.append(px + ((0,) if rgbw else ()))
    return out


def universes_for(first, address, count, ch):
    """Doc 14's spilling: whole pixels, the first universe from `address`, the rest from 1."""
    chunks, pixel, slot, u = [], 0, address - 1, first
    while pixel < count:
        room = (512 - slot) // ch
        if room == 0:
            slot, u = 0, u + 1
            continue
        take = min(room, count - pixel)
        chunks.append((u, slot, pixel, take))
        pixel, slot, u = pixel + take, 0, u + 1
    return chunks


def cmd_send(a):
    ch = 4 if a.rgbw else 3
    rgb = tuple(int(x) for x in a.rgb.split(","))
    chunks = universes_for(a.universe, a.address, a.count, ch)
    cid = uuid.uuid4().bytes
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 4)
    print("sending %s: %d pixels over universes %s at priority %d, %s" % (
        a.pattern, a.count, [c[0] for c in chunks], a.priority, "to " + a.to if a.to else "multicast"))
    seq, start = 0, time.time()
    try:
        while a.seconds <= 0 or time.time() - start < a.seconds:
            px = frame(a.pattern, a.count, time.time() - start, a.rgbw, rgb)
            for u, slot, first, take in chunks:
                slots = [0] * 512
                for k in range(take):
                    slots[slot + k * ch: slot + k * ch + ch] = px[first + k]
                dest = a.to or "239.255.%d.%d" % (u >> 8, u & 0xFF)
                sock.sendto(e131_packet(cid, u, seq, a.priority, slots), (dest, PORT))
            seq += 1
            time.sleep(1.0 / a.fps)
    except KeyboardInterrupt:
        pass
    if a.terminate:
        for _ in range(3):
            for u, _, _, _ in chunks:
                dest = a.to or "239.255.%d.%d" % (u >> 8, u & 0xFF)
                sock.sendto(e131_packet(cid, u, seq, a.priority, [0] * 512, options=0x40), (dest, PORT))
            seq += 1
        print("terminated")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("show"); p.add_argument("host"); p.set_defaults(fn=cmd_show)
    p = sub.add_parser("mode"); p.add_argument("host"); p.add_argument("output", type=int)
    p.add_argument("mode", choices=["effect", "sacn"])
    p.add_argument("--count", type=int); p.add_argument("--order"); p.add_argument("--universe", type=int)
    p.add_argument("--address", type=int); p.add_argument("--hold-ms", dest="hold_ms", type=int)
    p.set_defaults(fn=cmd_mode)
    p = sub.add_parser("send"); p.add_argument("pattern", choices=["rainbow", "chase", "solid"])
    p.add_argument("--universe", type=int, required=True); p.add_argument("--address", type=int, default=1)
    p.add_argument("--count", type=int, default=60); p.add_argument("--rgbw", action="store_true")
    p.add_argument("--rgb", default="255,0,0"); p.add_argument("--priority", type=int, default=100)
    p.add_argument("--fps", type=float, default=40); p.add_argument("--seconds", type=float, default=0)
    p.add_argument("--to"); p.add_argument("--terminate", action="store_true")
    p.set_defaults(fn=cmd_send)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
