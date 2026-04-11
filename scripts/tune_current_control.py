"""
Current-control PI tuning tool for test_motor_control firmware.

Runs automated stall tests: sets PI gains via serial, sends M1 TIGHTEN,
captures high-speed FAST log data at 100 Hz, sends STOP after timeout or
stall, then plots current, PWM, and RPM vs time.

Can sweep Kp values to compare controller response at different gains.

Usage:
    python scripts/tune_current_control.py --port COM9
    python scripts/tune_current_control.py --port COM9 --sweep-kp 0.5 1.0 2.0 4.0
    python scripts/tune_current_control.py --port COM9 --kp 1.0 --ki 0.1

Requirements:
    pip install pyserial matplotlib numpy
"""

import argparse
import re
import serial
import sys
import threading
import time
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def send_cmd(ser, cmd, echo=True):
    """Send a command and give the MCU time to process."""
    ser.write((cmd + "\n").encode())
    ser.flush()
    time.sleep(0.05)
    if echo:
        print(f"  >> {cmd}")


def reset_board(ser):
    """Toggle RTS to reset the ESP32."""
    ser.dtr = False
    ser.rts = False
    time.sleep(0.1)
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False


def drain(ser, timeout=0.3):
    """Read and discard all pending serial data."""
    ser.timeout = timeout
    while ser.read(4096):
        pass
    ser.timeout = 1


def capture_lines(ser, stop_event, lines):
    """Background thread: read serial lines until stop_event is set."""
    ser.timeout = 0.05
    while not stop_event.is_set():
        try:
            raw = ser.readline()
            if raw:
                line = raw.decode("utf-8", errors="replace").strip()
                lines.append(line)
        except Exception:
            pass


def run_trial(ser, kp, ki, duration_s=1.5, motor_cmd="M1 TIGHTEN"):
    """
    Run a single stall trial with given Kp/Ki.

    Returns dict with arrays: time_ms, current_mA, pwm, rpm, integral,
    plus metadata: kp, ki, stalled (bool), stall_time_ms.
    """
    print(f"\n{'='*50}")
    print(f"Trial: Kp={kp:.3f}  Ki={ki:.4f}")
    print(f"{'='*50}")

    # Ensure motor is stopped
    send_cmd(ser, "STOP", echo=False)
    time.sleep(0.2)
    drain(ser)

    # Set gains
    send_cmd(ser, f"KP {kp}")
    send_cmd(ser, f"KI {ki}")

    # Enable fast logging
    drain(ser)
    send_cmd(ser, "FAST")
    time.sleep(0.05)

    # Start capturing
    lines = []
    stop_event = threading.Event()
    reader = threading.Thread(target=capture_lines, args=(ser, stop_event, lines))
    reader.start()

    # Start motor
    send_cmd(ser, motor_cmd)

    # Wait for trial duration or stall
    t_start = time.time()
    stalled = False
    while time.time() - t_start < duration_s:
        # Check for stall in recent lines
        for line in lines[-10:]:
            if "STALLED" in line:
                stalled = True
                break
        if stalled:
            time.sleep(0.05)  # capture a few more data points after stall
            break
        time.sleep(0.01)

    # Stop motor and logging
    send_cmd(ser, "STOP", echo=False)
    time.sleep(0.1)
    send_cmd(ser, "FAST", echo=False)
    time.sleep(0.1)
    stop_event.set()
    reader.join(timeout=1)

    # Parse FAST log lines
    # Format: FAST,time_ms,motor,current_mA,pwm,rpm,integral
    data = {"time_ms": [], "current_mA": [], "pwm": [], "rpm": [], "integral": []}
    t0 = None
    for line in lines:
        if line.startswith("FAST,"):
            parts = line.split(",")
            if len(parts) >= 7:
                try:
                    t = int(parts[1])
                    if t0 is None:
                        t0 = t
                    data["time_ms"].append(t - t0)
                    data["current_mA"].append(float(parts[3]))
                    data["pwm"].append(int(parts[4]))
                    data["rpm"].append(float(parts[5]))
                    data["integral"].append(float(parts[6]))
                except (ValueError, IndexError):
                    pass

    # Check for stall in log
    stall_time = None
    for line in lines:
        if "STALLED" in line:
            stalled = True
            m = re.search(r"FAST,(\d+)", line)
            if m and t0 is not None:
                stall_time = int(m.group(1)) - t0
            break

    n = len(data["time_ms"])
    print(f"  Captured {n} samples ({n*10} ms)")
    if stalled:
        print(f"  Motor STALLED at t={stall_time} ms" if stall_time else "  Motor STALLED")
    else:
        print(f"  Motor did NOT stall within {duration_s}s")

    if n > 0:
        peak_ma = max(data["current_mA"])
        print(f"  Peak current: {peak_ma:.0f} mA")

    return {
        "kp": kp, "ki": ki,
        "stalled": stalled, "stall_time_ms": stall_time,
        "n_samples": n,
        **{k: np.array(v) for k, v in data.items()},
    }


def plot_trial(trial, ax_current, ax_pwm, ax_rpm, label=None, limit_ma=None):
    """Plot one trial onto three axes."""
    if trial["n_samples"] == 0:
        return

    t = trial["time_ms"]
    lbl = label or f"Kp={trial['kp']:.2f}"

    ax_current.plot(t, trial["current_mA"], label=lbl, linewidth=0.8)
    ax_pwm.plot(t, trial["pwm"], label=lbl, linewidth=0.8)
    ax_rpm.plot(t, trial["rpm"], label=lbl, linewidth=0.8)

    if trial["stalled"] and trial["stall_time_ms"] is not None:
        for ax in (ax_current, ax_pwm, ax_rpm):
            ax.axvline(trial["stall_time_ms"], color="red", linestyle="--",
                       alpha=0.5, linewidth=0.7)


def plot_results(trials, limit_ma, save_path=None):
    """Create a 3-row figure with current, PWM, RPM for all trials."""
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(12, 8), sharex=True)

    for trial in trials:
        label = f"Kp={trial['kp']:.2f} Ki={trial['ki']:.3f}"
        plot_trial(trial, ax1, ax2, ax3, label=label, limit_ma=limit_ma)

    # Current axis
    ax1.set_ylabel("Current (mA)")
    ax1.set_title("Stall Test — Current Control Response")
    ax1.axhline(limit_ma, color="red", linestyle=":", alpha=0.7,
                label=f"Limit: {limit_ma:.0f} mA")
    ax1.legend(fontsize=8, loc="upper right")
    ax1.grid(True, alpha=0.3)

    # PWM axis
    ax2.set_ylabel("PWM (0-255)")
    ax2.axhline(102, color="gray", linestyle=":", alpha=0.5, label="PWM_MIN (40%)")
    ax2.legend(fontsize=8, loc="upper right")
    ax2.grid(True, alpha=0.3)

    # RPM axis
    ax3.set_ylabel("Motor RPM")
    ax3.set_xlabel("Time (ms)")
    ax3.legend(fontsize=8, loc="upper right")
    ax3.grid(True, alpha=0.3)

    plt.tight_layout()

    if save_path:
        plt.savefig(save_path, dpi=150)
        print(f"\nPlot saved to {save_path}")

    plt.show()


def main():
    parser = argparse.ArgumentParser(description="PI current-control tuning tool")
    parser.add_argument("--port", required=True, help="Serial port (e.g. COM9)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--kp", type=float, default=1.0, help="Proportional gain")
    parser.add_argument("--ki", type=float, default=0.1, help="Integral gain")
    parser.add_argument("--duration", type=float, default=1.5,
                        help="Max trial duration in seconds")
    parser.add_argument("--motor", default="M1 TIGHTEN",
                        help="Motor command to test")
    parser.add_argument("--limit", type=float, default=None,
                        help="Current limit in mA (set on MCU). None = use existing.")
    parser.add_argument("--sweep-kp", type=float, nargs="+",
                        help="Sweep these Kp values (runs multiple trials)")
    parser.add_argument("--sweep-ki", type=float, nargs="+",
                        help="Sweep these Ki values (runs multiple trials)")
    parser.add_argument("--save", type=str, default=None,
                        help="Save plot to file path")
    parser.add_argument("--pause", type=float, default=2.0,
                        help="Pause between sweep trials (seconds)")
    args = parser.parse_args()

    print(f"Connecting to {args.port} @ {args.baud} baud...")
    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(0.5)
    drain(ser)

    # Optionally set current limit
    if args.limit is not None:
        send_cmd(ser, f"LIMIT T {args.limit}")
        time.sleep(0.1)

    # Read current limit from STATUS
    send_cmd(ser, "STATUS", echo=False)
    time.sleep(0.2)
    limit_ma = args.limit
    while ser.in_waiting:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        # Try to pick up the tighten limit from M1 line
        m = re.search(r"M1: T=(\d+)", line)
        if m and limit_ma is None:
            limit_ma = float(m.group(1))
    if limit_ma is None:
        limit_ma = 330  # fallback

    drain(ser)
    print(f"Current limit (tighten): {limit_ma:.0f} mA")

    # Build list of (kp, ki) trials
    trials_params = []
    if args.sweep_kp:
        for kp in args.sweep_kp:
            trials_params.append((kp, args.ki))
    elif args.sweep_ki:
        for ki in args.sweep_ki:
            trials_params.append((args.kp, ki))
    else:
        trials_params.append((args.kp, args.ki))

    # Run trials
    results = []
    for i, (kp, ki) in enumerate(trials_params):
        result = run_trial(ser, kp, ki,
                           duration_s=args.duration,
                           motor_cmd=args.motor)
        results.append(result)

        # Pause between trials for motor to cool / current to settle
        if i < len(trials_params) - 1:
            print(f"\n  Pausing {args.pause}s before next trial...")
            time.sleep(args.pause)

    ser.close()

    # Plot
    if any(r["n_samples"] > 0 for r in results):
        save_path = args.save
        if save_path is None:
            save_path = str(Path(__file__).parent.parent
                           / "test" / "test_motor_control"
                           / "tune_results.png")
        plot_results(results, limit_ma, save_path=save_path)
    else:
        print("\nNo data captured — check serial connection and firmware version.")

    # Print summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"{'Kp':>8} {'Ki':>8} {'Samples':>8} {'Peak mA':>8} {'Stalled':>8}")
    for r in results:
        peak = f"{max(r['current_mA']):.0f}" if r["n_samples"] > 0 else "N/A"
        stall_str = "YES" if r["stalled"] else "no"
        print(f"{r['kp']:8.3f} {r['ki']:8.4f} {r['n_samples']:8d} {peak:>8} {stall_str:>8}")


if __name__ == "__main__":
    main()
