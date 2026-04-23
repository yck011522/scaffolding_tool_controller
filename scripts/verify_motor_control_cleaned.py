"""Verify test_motor_control_cleaned firmware over USB serial.

Resets the board, captures boot output, then exercises the new
per-motor command parser (KP/KI/RAMP/SLEW/PWMMIN/PWMMAX with optional
M1/M2 selector). It does NOT command motors to actually run — just
confirms parsing, range checks, and STATUS reflection.

Usage:
    python scripts/verify_motor_control_cleaned.py [COM22]

Requires: pyserial
"""

import re
import sys
import threading
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM22"
BAUD = 115200

captured: list[str] = []
stop_flag = threading.Event()


def reader(ser: serial.Serial) -> None:
    while not stop_flag.is_set():
        try:
            line = ser.readline().decode("utf-8", errors="replace")
        except serial.SerialException:
            break
        if line:
            captured.append(line.rstrip())
            print(f"[SERIAL] {line.rstrip()}")


def reset(ser: serial.Serial) -> None:
    ser.dtr = False
    ser.rts = False
    time.sleep(0.1)
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False


def send(ser: serial.Serial, cmd: str, settle: float = 0.4) -> None:
    print(f"[SEND]   {cmd}")
    ser.write((cmd + "\n").encode())
    time.sleep(settle)


def find(pattern: str, since: int = 0) -> str | None:
    rx = re.compile(pattern)
    for line in captured[since:]:
        if rx.search(line):
            return line
    return None


def expect(pattern: str, since: int, label: str) -> bool:
    hit = find(pattern, since)
    if hit:
        print(f"[OK]     {label}: {hit}")
        return True
    print(f"[FAIL]   {label}: pattern {pattern!r} not seen since line {since}")
    return False


def main() -> int:
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
    t = threading.Thread(target=reader, args=(ser,), daemon=True)
    t.start()

    try:
        print("[INFO] Resetting board...")
        reset(ser)
        time.sleep(5)  # boot + cal sweep (~1s + 20*50ms)

        boot_idx = 0
        ok = True
        ok &= expect(r"=== test_motor_control_cleaned ===", boot_idx, "banner")
        ok &= expect(r"Baseline offset:", boot_idx, "current sensor calibrated")
        ok &= expect(r"Commands:", boot_idx, "help printed")

        # ── Initial STATUS shows defaults from cfg[] ──────────────
        idx = len(captured)
        send(ser, "STATUS", settle=0.6)
        ok &= expect(r"M1: limit T=330 L=900", idx, "M1 defaults")
        ok &= expect(r"M2: limit T=330 L=900", idx, "M2 defaults")
        ok &= expect(r"pwm\[min=102 max=255\]", idx, "pwmMin/pwmMax defaults")
        ok &= expect(r"ceiling T=160 L=160", idx, "pwmCeiling defaults")
        ok &= expect(r"slew T=15 L=500", idx, "slew defaults")

        # ── Per-motor LIMIT ───────────────────────────────────────
        idx = len(captured)
        send(ser, "LIMIT M1 T 250")
        ok &= expect(r"\[CFG\] M1 tighten limit set to 250", idx, "LIMIT M1 T")

        idx = len(captured)
        send(ser, "LIMIT M2 L 750")
        ok &= expect(r"\[CFG\] M2 loosen limit set to 750", idx, "LIMIT M2 L")

        # ── Per-motor KP / KI ─────────────────────────────────────
        idx = len(captured)
        send(ser, "KP M1 0.5")
        ok &= expect(r"\[CFG\] M1 Kp = 0\.500", idx, "KP M1")

        idx = len(captured)
        send(ser, "KI M2 1.5")
        ok &= expect(r"\[CFG\] M2 Ki = 1\.5000", idx, "KI M2")

        # ── Per-motor RAMP ────────────────────────────────────────
        idx = len(captured)
        send(ser, "RAMP M1 350")
        ok &= expect(r"\[CFG\] M1 ramp = 350 ms", idx, "RAMP M1")

        # ── Per-motor SLEW ────────────────────────────────────────
        idx = len(captured)
        send(ser, "SLEW M2 T 25")
        ok &= expect(r"\[CFG\] M2 slew tighten = 25", idx, "SLEW M2 T")

        # ── PWMMIN / PWMMAX ───────────────────────────────────────
        idx = len(captured)
        send(ser, "PWMMIN M1 110")
        ok &= expect(r"\[CFG\] M1 pwmMin = 110", idx, "PWMMIN M1")

        idx = len(captured)
        send(ser, "PWMMAX M2 L 180")
        ok &= expect(r"\[CFG\] M2 pwmMax loosen = 180", idx, "PWMMAX M2 L")

        # ── Range-check rejection ─────────────────────────────────
        idx = len(captured)
        send(ser, "LIMIT 9999")
        ok &= expect(r"\[ERR\] LIMIT must be 50-1500 mA", idx, "LIMIT range check")

        # ── Final STATUS reflects every change ────────────────────
        idx = len(captured)
        send(ser, "STATUS", settle=0.6)
        ok &= expect(r"M1: limit T=250 L=900 mA  Kp=0\.500 Ki=1\.0000  ramp=350ms",
                     idx, "M1 final state")
        ok &= expect(r"M2: limit T=330 L=750 mA  Kp=0\.000 Ki=1\.5000  ramp=200ms",
                     idx, "M2 final state")
        ok &= expect(r"pwm\[min=110 max=255\]", idx, "M1 pwmMin updated")
        ok &= expect(r"ceiling T=160 L=180", idx, "M2 ceiling L updated")
        ok &= expect(r"slew T=25 L=500", idx, "M2 slew T updated")

        # ── STOP / IDLE sanity ────────────────────────────────────
        idx = len(captured)
        send(ser, "STOP")
        # No motor was running; should still respond cleanly
        # (no output expected — just verify subsequent STATUS still works)
        idx = len(captured)
        send(ser, "STATUS", settle=0.5)
        ok &= expect(r"Active motor: none", idx, "active=none after STOP")

        print()
        print("[RESULT]", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        stop_flag.set()
        time.sleep(0.3)
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
