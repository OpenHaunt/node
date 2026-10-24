#!/usr/bin/env python3
"""Minimal RTP sender for the OH Node: L16, 48 kHz, stereo, evenly paced packets.

Decodes any audio/video file with ffmpeg and sends one RTP packet every --packet-ms.
Reference for integrating the stream into an own application: all it takes is a UDP socket
and a 12-byte header per packet.

    python3 rtp_send.py song.mp3 192.168.114.176 [--port 5004] [--packet-ms 5] [--start 40] [--duration 25]
"""
import argparse
import random
import socket
import struct
import subprocess
import time

SAMPLE_RATE = 48000
CHANNELS = 2
BYTES_PER_FRAME = 2 * CHANNELS
PAYLOAD_TYPE = 97  # dynamic payload type, L16/48000/2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("host")
    ap.add_argument("--port", type=int, default=5004)
    ap.add_argument("--packet-ms", type=float, default=5.0)
    ap.add_argument("--start", type=float, default=0)
    ap.add_argument("--duration", type=float)
    args = ap.parse_args()

    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-ss", str(args.start), "-i", args.file, "-vn"]
    if args.duration:
        cmd += ["-t", str(args.duration)]
    # raw PCM, big-endian 16 bit = RTP L16 payload as is
    cmd += ["-ar", str(SAMPLE_RATE), "-ac", str(CHANNELS), "-f", "s16be", "-"]
    decoder = subprocess.Popen(cmd, stdout=subprocess.PIPE)

    frames = int(SAMPLE_RATE * args.packet_ms / 1000)
    chunk = frames * BYTES_PER_FRAME
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    seq = random.randrange(1 << 16)
    timestamp = random.randrange(1 << 32)
    ssrc = random.randrange(1 << 32)

    interval = frames / SAMPLE_RATE
    next_send = None
    sent = 0
    while True:
        payload = decoder.stdout.read(chunk)
        if len(payload) < BYTES_PER_FRAME:
            break
        if next_send is None:  # start the clock with the first audio, not before the decoder is ready
            next_send = time.perf_counter()
        payload = payload[: len(payload) - len(payload) % BYTES_PER_FRAME]
        # V=2, no padding/extension/CSRC; marker on the first packet
        header = struct.pack("!BBHII", 0x80, PAYLOAD_TYPE | (0x80 if sent == 0 else 0), seq, timestamp, ssrc)
        while True:  # sleep coarse, spin the last bit for even spacing
            wait = next_send - time.perf_counter()
            if wait <= 0:
                break
            time.sleep(wait - 0.001 if wait > 0.002 else 0)
        sock.sendto(header + payload, (args.host, args.port))
        seq = (seq + 1) & 0xFFFF
        timestamp = (timestamp + len(payload) // BYTES_PER_FRAME) & 0xFFFFFFFF
        next_send += interval
        sent += 1
    decoder.wait()
    print(f"sent {sent} packets")


if __name__ == "__main__":
    main()
