#!/usr/bin/env python3
"""Reset the board via RTS and print what it sends (diagnostics).
Usage: python tools/serial_probe.py COM4 [seconds] [--boot]
  --boot uses the classic auto-download sequence (IO0 low while EN rises).
"""
import sys
import time

import serial

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 4
boot = "--boot" in sys.argv
s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.1
s.dtr = False
s.rts = False
s.open()
s.dtr = False
s.rts = True          # EN low
time.sleep(0.15)
if boot:
    s.dtr = True      # IO0 low
    s.rts = False     # EN high -> boots with IO0 low
    time.sleep(0.5)
    s.dtr = False
else:
    s.rts = False
end = time.time() + secs
buf = b""
while time.time() < end:
    buf += s.read(4096)
s.close()
sys.stdout.buffer.write(buf[:1500])
print()
