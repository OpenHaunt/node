#!/usr/bin/env python3
"""A dependency-free MQTT 3.1.1 subscriber, enough to watch a node.

    scripts/mqtt-sub.py 192.168.11.184:1883 'openhaunt/#' --seconds 30
    scripts/mqtt-sub.py host:port 'openhaunt/+/output/+/set' --pub openhaunt/0c181818/output/0/set '{"state":true}'
    scripts/mqtt-sub.py host:port 'openhaunt/#' --retain --pub openhaunt/clock '{"t":1756550400123,"seq":1}'
"""
import argparse, socket, struct, sys, time

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

def read_packet(sock, deadline):
    sock.settimeout(max(0.05, deadline - time.time()))
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

ap = argparse.ArgumentParser()
ap.add_argument("broker")
ap.add_argument("topic", nargs="?", default="openhaunt/#")
ap.add_argument("--seconds", type=float, default=20)
ap.add_argument("--pub", nargs=2, action="append", default=[], metavar=("TOPIC", "PAYLOAD"))
ap.add_argument("--client", default="oh-mqtt-sub")
# The console's clock is retained, so injecting one by hand has to be too: without
# the flag a node that subscribes afterwards never sees it.
ap.add_argument("--retain", action="store_true", help="publish with the retain flag set")
a = ap.parse_args()

host, _, port = a.broker.rpartition(":")
sock = socket.create_connection((host or a.broker, int(port or 1883)), timeout=5)
connect = enc_str("MQTT") + bytes([4, 0x02]) + struct.pack("!H", 30) + enc_str(a.client)
sock.sendall(packet(0x10, connect))
kind, body = read_packet(sock, time.time() + 5)
if kind != 0x20 or body[1] != 0:
    sys.exit("connect refused: %r" % (body,))
sock.sendall(packet(0x82, struct.pack("!H", 1) + enc_str(a.topic) + bytes([1])))
for topic, payload in a.pub:
    sock.sendall(packet(0x30 | (0x01 if a.retain else 0), enc_str(topic) + payload.encode()))
    print("> %s %s%s" % (topic, payload, " (retained)" if a.retain else ""))
deadline = time.time() + a.seconds
last_ping = time.time()
while time.time() < deadline:
    try:
        kind, body = read_packet(sock, min(deadline, time.time() + 10))
    except socket.timeout:
        sock.sendall(packet(0xC0, b""))
        continue
    if kind is None:
        break
    if kind & 0xF0 == 0x30:
        qos = (kind >> 1) & 3
        tlen = struct.unpack("!H", body[:2])[0]
        topic = body[2:2 + tlen].decode()
        rest = body[2 + tlen:]
        if qos:
            pid, rest = struct.unpack("!H", rest[:2])[0], rest[2:]
            sock.sendall(packet(0x40, struct.pack("!H", pid)))
        print("%s %s %s" % (time.strftime("%H:%M:%S"), topic, rest.decode("utf-8", "replace")))
        sys.stdout.flush()
    if time.time() - last_ping > 10:
        sock.sendall(packet(0xC0, b""))
        last_ping = time.time()
sock.close()
