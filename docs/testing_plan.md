# Testing Plan

Progress tracker for individual component tests and integration steps.

Each test folder under `test/` contains its own firmware and should save test results as timestamped files (e.g., `results_2026-04-01.md`) so that results from different hardware or motor variants are preserved.

## Test Sequence

| # | Test | Status | Depends on | Notes |
|---|---|---|---|---|
| 1 | `test_bldc_pwm` — PWM drive + direction control | ✅ Verified on S3-Tiny (covered by #2) | — | Tested on XIAO 2026-04-01. Not re-run separately on S3-Tiny — PWM drive is exercised by feedback and current tests. Pins updated for Motor 1 (GPIO5/6). |
| 2 | `test_bldc_feedback` — Pulse counting + RPM | ✅ Verified on S3-Tiny 2026-04-09 | #1 | Identical results to XIAO. CAP pin (GPIO7) with 22k/47k divider. |
| 3 | `test_ina_current` — INA240 current reading | ✅ Verified on S3-Tiny 2026-04-09 | #1 | ADC (GPIO13). Tested with 56:1 motor. Results match XIAO within noise. |
| 4 | `test_rs485` — Echo / loopback with USB adapter | ✅ Verified on S3-Tiny 2026-04-09 | — | GPIO43/44 TX/RX + GPIO18 DE/RE. 0 errors, identical latency to XIAO. |
| 5 | `test_oled` — SSD1306 display | ✅ Verified on S3-Tiny 2026-04-09 | — | Upgraded to 0.96″ (128×64). I2C on GPIO15/16. No offset needed. |
| 6 | `test_buttons` — Individual GPIO button read | ✅ Verified on S3-Tiny 2026-04-09 | — | GPIO1-4, internal pull-up, active LOW. Required `ARDUINO_USB_CDC_ON_BOOT=1` fix for serial input. |
| 7 | ~~`test_camera` — OV3660 MJPEG over Wi-Fi~~ | Removed | — | Camera is on a separate XIAO ESP32S3 Sense module, not on this board. |
| 8 | Motor control loop integration | ✅ Verified on bench (S3-Tiny) — see `src/motor_control.cpp`. Stall path confirmed manually by jamming the gearbox under load (`ERR 2 STALLED` raised, `STOP` clears it). | #1–3 | PI current loop + soft-start ramp + slew limit + stall detection. Tunable per motor / per direction; persisted to NVS. |
| 9 | USB + RS-485 command interface | ✅ Verified 2026-04-23 — `scripts/verify_controller.py` 148/148 PASS | #4, #8 | Single shared parser. USB-CDC and RS-485 accept the same grammar; LOG/FAST/STATUSV/HELP are USB-only. See `docs/protocol.md`. |
| 10 | Button overrides + OLED status | ✅ Verified on bench | #6, #5, #8 | GPIO1–4 debounced; buttons preempt serial commands. OLED shows per-motor state, current, PWM%. |
| 11 | ~~Camera streaming integration~~ | Removed (separate repo) | — | Camera lives in `scaffolding_camera_controller` on a XIAO ESP32-S3 Sense. |
| 12 | Final pin lockdown + full system test on UR5e | Not started | All above | Mount on tool flange, run TIGHTEN/LOOSEN sequences under real mechanical load, confirm stall detection trips on a jammed joint, soak-test for thermal behaviour. |

## Test Result Convention

Each test folder should contain result files named:

```
results_YYYY-MM-DD[_variant].md
```

For example:
- `test/test_bldc_pwm/results_2026-04-01_ratio56.md`
- `test/test_bldc_pwm/results_2026-04-01_ratio90.md`
- `test/test_bldc_feedback/results_2026-04-02.md`

This allows us to keep historical records when retesting with different motors or hardware revisions.
