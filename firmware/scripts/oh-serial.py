#!/usr/bin/env python3
"""Talk to a node's serial console without a terminal.

One-shot (opening the port resets the chip on macOS, so this always shows a boot):
    scripts/oh-serial.py /dev/cu.usbserial-110 --seconds 20
    scripts/oh-serial.py /dev/cu.usbserial-110 --send "oh info" --seconds 5

Persistent session, so commands can be sent without a reboot in between:
    scripts/oh-serial.py /dev/cu.usbserial-110 --daemon build/serial      # keeps running
    echo "oh info" > build/serial.fifo ; tail -20 build/serial.log
"""
import argparse, errno, os, sys, time
import serial  # pyserial, in the IDF venv

ap = argparse.ArgumentParser()
ap.add_argument("port")
ap.add_argument("--baud", type=int, default=115200)
ap.add_argument("--seconds", type=float, default=5)
ap.add_argument("--reset", action="store_true", help="pulse RTS/DTR to reset the chip first")
ap.add_argument("--send", action="append", default=[], help="line to send after --wait seconds")
ap.add_argument("--wait", type=float, default=0.5, help="seconds before/between sends")
ap.add_argument("--grep", default=None, help="only print lines containing this")
ap.add_argument("--daemon", default=None, help="prefix for <prefix>.log and <prefix>.fifo; run until killed")
a = ap.parse_args()

s = serial.Serial()
s.port, s.baudrate, s.timeout = a.port, a.baud, 0.1
s.dtr = False
s.rts = False
s.open()
if a.reset:
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False

def strip_ansi(text):
    out, i = [], 0
    while i < len(text):
        if text[i] == "\x1b" and i + 1 < len(text) and text[i + 1] == "[":
            j = i + 2
            while j < len(text) and not text[j].isalpha():
                j += 1
            i = j + 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)

if a.daemon:
    log = open(a.daemon + ".log", "ab", buffering=0)
    fifo_path = a.daemon + ".fifo"
    if not os.path.exists(fifo_path):
        os.mkfifo(fifo_path)
    fifo = os.open(fifo_path, os.O_RDONLY | os.O_NONBLOCK)
    pending = b""
    while True:
        data = s.read(4096)
        if data:
            log.write(data)
        try:
            cmd = os.read(fifo, 4096)
        except OSError as e:
            cmd = b"" if e.errno == errno.EAGAIN else b""
        if cmd:
            pending += cmd
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                line = line.strip()
                if line == b"!reset":
                    s.dtr = False
                    s.rts = True
                    time.sleep(0.1)
                    s.rts = False
                elif line:
                    s.write(line + b"\n")
                    time.sleep(0.05)
        if not data and not cmd:
            time.sleep(0.02)

end = time.time() + a.seconds
sends = list(a.send)
next_send = time.time() + a.wait
buf = b""
while time.time() < end:
    data = s.read(4096)
    if data:
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = strip_ansi(line.decode("utf-8", "replace").rstrip("\r"))
            if a.grep is None or a.grep in text:
                print(text)
                sys.stdout.flush()
    if sends and time.time() >= next_send:
        s.write((sends.pop(0) + "\n").encode())
        next_send = time.time() + a.wait
if buf:
    print(strip_ansi(buf.decode("utf-8", "replace")))
