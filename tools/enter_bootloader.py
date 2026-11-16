#!/usr/bin/env python3
"""Put an ESP32 into download mode via DTR/RTS with slower timing than esptool's
default (some CH340 CYD boards need it). Then flash with --before no_reset.

Usage: python tools/enter_bootloader.py COM4 [delay_s]
"""
import sys
import time

import serial

port = sys.argv[1]
delay = float(sys.argv[2]) if len(sys.argv) > 2 else 0.4
s = serial.Serial()
s.port = port
s.baudrate = 115200
s.dtr = False
s.rts = False
s.open()
# EN low (RTS=1), IO0 high
s.dtr = False
s.rts = True
time.sleep(0.2)
# IO0 low (DTR=1), release EN (RTS=0)
s.dtr = True
s.rts = False
time.sleep(delay)
# release IO0
s.dtr = False
time.sleep(0.05)
s.close()
print("reset into bootloader requested")
