"""Reinicia o M5Stick e mostra o log serial por alguns segundos.

uso: python tools/serial_log.py [porta] [segundos]
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 20

s = serial.Serial(port, 115200, timeout=0.2)
s.setDTR(False)
s.setRTS(True)  # segura o reset
time.sleep(0.3)
s.setRTS(False)

end = time.time() + seconds
while time.time() < end:
    data = s.read(4096)
    if data:
        sys.stdout.write(data.decode("ascii", "replace").replace("�", "?"))
        sys.stdout.flush()
s.close()
