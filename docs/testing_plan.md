# Testing Plan

Progress tracker for individual component tests and integration steps.

Each test folder under `test/` contains its own firmware and should save test results as timestamped files (e.g., `results_2026-04-01.md`) so that results from different hardware or motor variants are preserved.

## Test Sequence

| # | Test | Status | Depends on | Notes |
|---|---|---|---|---|
| 1 | `test_bldc_pwm` — PWM drive + direction control | Tested on XIAO, not yet on S3-Tiny | — | Test all 4 gearbox ratios. Verify 3.3 V PWM accepted by driver. Pins updated for Motor 1 (GPIO5/6). |
| 2 | `test_bldc_feedback` — Pulse counting + RPM | Tested on XIAO, not yet on S3-Tiny | #1 | Sweep PWM duty cycles, log RPM vs duty. Find dead band. Needs 5 V → 3.3 V divider on CAP pin (GPIO7). |
| 3 | `test_ina_current` — INA240 current reading | Tested on XIAO, not yet on S3-Tiny | #1 | Read analog output (GPIO13) while motor runs at known duty cycles. Cross-check with external multimeter. |
| 4 | `test_rs485` — Echo / loopback with USB adapter | Tested on XIAO, not yet on S3-Tiny | — | USB-C serial for commands, RS-485 via GPIO43/44 TX/RX + GPIO18 DE/RE. Test both directions. |
| 5 | `test_oled` — SSD1306 display | Tested on XIAO, not yet on S3-Tiny | — | I2C on GPIO15/16, verify address 0x3C. Display static text. |
| 6 | `test_buttons` — Individual GPIO button read | Not started | — | 4 buttons on GPIO1-4, internal pull-up, active LOW. Verify press/release detection. |
| 7 | ~~`test_camera` — OV3660 MJPEG over Wi-Fi~~ | Removed | — | Camera is on a separate XIAO ESP32S3 Sense module, not on this board. |
| 8 | Motor control loop integration | Not started | #1–3 | PWM + feedback + INA current limiting + stall detection. |
| 9 | RS-485 command interface | Not started | #4, #8 | Parse commands from both USB-C and RS-485. Reply on originating port (or both). |
| 10 | Button overrides + OLED status | Not started | #6, #5, #8 | Buttons preempt RS-485 commands. OLED shows live state. |
| 11 | Camera streaming integration | Not started | #7, #9 | On-demand via command. Dual-core. |
| 12 | Final pin lockdown + full system test | Not started | All above | Finalize Grove connector assignments. Test on UR5e. |

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
