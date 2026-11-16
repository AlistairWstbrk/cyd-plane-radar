#!/usr/bin/env python3
"""Grab a screenshot from the CYD over USB serial and save it as PNG.

Usage:
  python tools/screenshot.py COM4 out.png [--cmd "sim 40"] [--cmd "screen list"] [--wait 2]

Each --cmd is sent first (with --wait seconds after each), then "shot".
The firmware switches to 921600 baud for the transfer and back afterwards.
Needs: pyserial, Pillow (both available in PlatformIO's Python: ~/.platformio/penv).
"""
import argparse
import sys
import time

import serial

try:
    from PIL import Image
except ImportError:  # fall back to a dependency-free PNG writer
    Image = None


def write_png(path, w, h, rgb):
    import struct
    import zlib
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def read_line(ser, timeout=10.0):
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        c = ser.read(1)
        if not c:
            continue
        if c == b"\n":
            return buf.decode(errors="replace").strip()
        buf += c
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("out")
    ap.add_argument("--cmd", action="append", default=[])
    ap.add_argument("--wait", type=float, default=1.5)
    args = ap.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False  # don't reset the board on open
    ser.rts = False
    ser.open()
    time.sleep(0.2)
    ser.reset_input_buffer()

    for c in args.cmd:
        ser.write((c + "\n").encode())
        time.sleep(args.wait)
        out = ser.read(ser.in_waiting or 1).decode(errors="replace")
        for line in out.splitlines():
            if line.strip():
                print("  <", line.strip())

    ser.reset_input_buffer()
    ser.write(b"shot\n")
    baud = None
    deadline = time.time() + 15
    while time.time() < deadline:
        line = read_line(ser, 5)
        if line and line.startswith("#BAUD"):
            baud = int(line.split()[1])
            break
    if baud is None:
        print("no #BAUD marker received", file=sys.stderr)
        return 1
    ser.baudrate = baud
    w = h = None
    deadline = time.time() + 10
    while time.time() < deadline:
        line = read_line(ser, 5)
        if line and line.startswith("#SCREENSHOT"):
            _, w, h = line.split()
            w, h = int(w), int(h)
            break
    if w is None:
        print("no #SCREENSHOT header", file=sys.stderr)
        return 1
    need = w * h * 2
    data = b""
    deadline = time.time() + 30
    while len(data) < need and time.time() < deadline:
        data += ser.read(need - len(data))
    ser.baudrate = 115200
    ser.close()
    if len(data) < need:
        print(f"short read {len(data)}/{need}", file=sys.stderr)
        return 1

    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        v = (data[2 * i] << 8) | data[2 * i + 1]
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        rgb[3 * i] = (r << 3) | (r >> 2)
        rgb[3 * i + 1] = (g << 2) | (g >> 4)
        rgb[3 * i + 2] = (b << 3) | (b >> 2)
    if Image is not None:
        Image.frombytes("RGB", (w, h), bytes(rgb)).save(args.out)
    else:
        write_png(args.out, w, h, bytes(rgb))
    print(f"saved {args.out} ({w}x{h})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
