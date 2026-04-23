# scripts/

Python helpers for bench bring-up, tuning, and verification of the
Scaffolding Tool Controller. All scripts assume the project's `.venv`
is active and `pyserial` is installed (`pip install pyserial`).

| Script | Purpose | Typical invocation |
|---|---|---|
| [`verify_controller.py`](verify_controller.py) | End-to-end protocol verification of the integrated firmware over **both** USB-CDC and RS-485. Snapshots the controller's current configuration, resets to factory defaults, exercises every documented command (handshake, status, motor TIGHTEN/LOOSEN, GET/SET round-trip, error cases, USB-only verbs), restores the original configuration, and prints a diff vs. defaults. Writes a timestamped results file (`results_YYYY-MM-DD.md`) next to itself on every run. | `python scripts/verify_controller.py` |
| [`factory_reset_config.py`](factory_reset_config.py) | Sends `RESET CONFIG` to the controller and prints the post-reset `GET CONFIG M1` / `GET CONFIG M2` readback so you can confirm the firmware's compiled-in defaults. | `python scripts/factory_reset_config.py` |
| [`tune_current_control.py`](tune_current_control.py) | Interactive PI / current-limit tuning helper. Streams live current and PWM telemetry from the controller while you adjust `KP`, `KI`, `LIMIT`, `SLEW`, `RAMP`, `PWMSTART`, etc. | `python scripts/tune_current_control.py` |
| [`verify_oled.py`](verify_oled.py) | Stand-alone OLED bring-up check (used during the `test_oled` subsystem build, not the integrated firmware). | `python scripts/verify_oled.py` |
| [`verify_motor_control_cleaned.py`](verify_motor_control_cleaned.py) | **Legacy.** Smoke test for the parser in `test/test_motor_control_cleaned/`, which has been superseded by `src/`. Kept for reference. | `python scripts/verify_motor_control_cleaned.py` |

## Common options

`verify_controller.py` is the most-used script and exposes:

| Flag | Purpose |
|---|---|
| `--usb-port COMxx` | Override the ESP32-S3 USB-CDC port (default `COM22`). |
| `--rs485-port COMxx` | Override the USB↔RS-485 adapter port (default `COM7`). |
| `--baud N` | Override the baud rate (default 115200). |
| `--skip-usb` / `--skip-rs485` | Run only one transport. |
| `--skip-motor` | Skip the TIGHTEN/LOOSEN exercises (config-only run, useful when the motors are unpowered). |
| `--no-restore` | Leave the controller on factory defaults instead of restoring the snapshot. |
| `--no-results-file` | Suppress writing `results_YYYY-MM-DD.md`. |

## Caution: USB-CDC bootloader trap

Any script that opens the controller's USB-C port **must** open it with
`dsrdtr=False, rtscts=False` (these are the `pyserial` defaults). Toggling
DTR/RTS while the firmware is running can cause the ESP32-S3 to enter ROM
download mode, which leaves the motor PWM pin floating until a physical
reset. See [`docs/hardware_specs.md`](../docs/hardware_specs.md) for the
full failure mode. The `Link.reset_board()` helper inside
`verify_controller.py` toggles RTS deliberately and only at start-up;
do not copy that pattern into long-running tools.
