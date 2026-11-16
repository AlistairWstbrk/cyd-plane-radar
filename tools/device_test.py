#!/usr/bin/env python3
"""On-device test run over USB serial: screenshots of every screen plus a
simulated-traffic stress test that records frame time and heap.

Usage: python tools/device_test.py COM4 [out_dir]

Uses the firmware's serial test commands (type "help" in a serial monitor).
Screenshots come from the frame buffer, so they verify layout and placement;
they cannot show panel colour inversion (check the screen itself for that).
"""
import os
import re
import subprocess
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
OUT = sys.argv[2] if len(sys.argv) > 2 else "device_shots"
HERE = os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT, exist_ok=True)


def open_port():
    s = serial.Serial()
    s.port = PORT
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    return s


def cmd(s, line, wait=1.0):
    s.reset_input_buffer()
    s.write((line + "\n").encode())
    time.sleep(wait)
    out = s.read(s.in_waiting or 1).decode(errors="replace")
    for l in out.splitlines():
        if l.strip():
            print("   ", l.strip())
    return out


def shot(name):
    path = os.path.join(OUT, name + ".png")
    subprocess.run([sys.executable, os.path.join(HERE, "screenshot.py"), PORT, path], check=False)


def main():
    s = open_port()
    print("== device info")
    cmd(s, "panel")
    cmd(s, "stats")
    s.close()

    steps = [
        ("sim 25", 4, "01_radar_sim25"),
        ("select 0", 1, "02_radar_selected"),
        ("screen list", 1, "03_list"),
        ("screen menu", 1, "04_menu"),
        ("screen airports", 1, "05_airports"),
        ("find tampa", 2, "05b_search_tampa"),
        ("find", 2, "05c_search_nearby"),
        ("screen info", 1, "06_info"),
        ("screen radar", 1, None),
        ("tap 280 100", 1, "06b_detail"),
        ("screen radar", 1, None),
        ("set theme 2", 1, "07_theme_ice"),
        ("set theme 0", 1, None),
    ]
    for c, w, name in steps:
        s = open_port()
        print("==", c)
        cmd(s, c, w)
        s.close()
        if name:
            shot(name)

    print("== stress: 80 aircraft, 60 s, sweep + trails + alt colours")
    s = open_port()
    for c in ("set sweep 1", "set altc 1", "range 5", "sim 80"):
        cmd(s, c, 0.5)
    frame_ms, heap_min = [], []
    end = time.time() + 60
    buf = ""
    while time.time() < end:
        buf += s.read(4096).decode(errors="replace")
    for m in re.finditer(r"ui: (\d+) ms/frame avg, (\d+) targets, heap (\d+) \(min (\d+)\)", buf):
        frame_ms.append(int(m.group(1)))
        heap_min.append(int(m.group(4)))
    if frame_ms:
        print(f"   frame time avg {sum(frame_ms) / len(frame_ms):.0f} ms (max {max(frame_ms)} ms), "
              f"min free heap {min(heap_min)} bytes")
    else:
        print("   no ui stats captured")
    s.close()
    shot("08_stress_80")
    s = open_port()
    for c in ("sim 0", "set sweep 0", "set altc 0", "range 2", "stats"):
        cmd(s, c, 0.5)
    s.close()
    print(f"done - screenshots in {OUT}/")


if __name__ == "__main__":
    main()
