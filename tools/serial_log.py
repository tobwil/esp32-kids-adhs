#!/usr/bin/env python3
"""Serielle Ausgabe des Geräts mitschreiben: serial_log.py <sekunden> [befehl ...] (Befehle werden zu Beginn gesendet)."""
import sys, time, serial, glob
port = (glob.glob('/dev/cu.usbmodem*') or ['/dev/cu.usbmodem13101'])[0]
dur = float(sys.argv[1]) if len(sys.argv) > 1 else 10
s = serial.Serial(port, 115200, timeout=0.2)
time.sleep(0.3)
for cmd in sys.argv[2:]:
    s.write((cmd + '\n').encode()); time.sleep(0.1)
end = time.time() + dur
while time.time() < end:
    line = s.readline()
    if line:
        print(f'{time.time() % 1000:7.2f} ' + line.decode(errors='replace').rstrip(), flush=True)
