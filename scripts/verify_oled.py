"""Verify OLED test firmware serial output.

Resets the board via RTS toggle, captures serial output, and checks
for expected [OLED] markers indicating successful I2C scan and
SSD1306 initialisation.

Usage:  python scripts/verify_oled.py
Requires: pyserial
"""

import serial
import time
import threading

PORT = "COM5"
BAUD = 115200
CAPTURE_SECONDS = 8

lines = []

def reader():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    # Reset the board via RTS toggle
    ser.dtr = False
    ser.rts = False
    time.sleep(0.1)
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False

    end = time.time() + CAPTURE_SECONDS
    while time.time() < end:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if line:
            lines.append(line)
            print(f"[SERIAL] {line}")
    ser.close()

t = threading.Thread(target=reader, daemon=True)
t.start()
t.join(timeout=CAPTURE_SECONDS + 3)

print()
print("--- OLED Test Results ---")
found_i2c   = any("Device found" in l for l in lines)
found_init  = any("SSD1306 init OK" in l for l in lines)
found_disp  = any("Display updated" in l for l in lines)
found_done  = any("Test complete" in l for l in lines)
found_fail  = any("init failed" in l or "No I2C devices" in l for l in lines)

print(f"  I2C device detected: {found_i2c}")
print(f"  SSD1306 init OK:     {found_init}")
print(f"  Display updated:     {found_disp}")
print(f"  Test complete:       {found_done}")
if found_fail:
    print(f"  [FAIL] Errors detected in serial output")
elif found_done:
    print(f"  [OK] OLED test passed")
else:
    print(f"  [WARN] Test may not have completed — check serial output above")
