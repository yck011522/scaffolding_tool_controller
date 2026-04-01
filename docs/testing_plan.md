# Testing Plan

Progress tracker for individual component tests and integration steps.

Each test folder under `test/` contains its own firmware and should save test results as timestamped files (e.g., `results_2026-04-01.md`) so that results from different hardware or motor variants are preserved.

## Test Sequence

| # | Test | Status | Depends on | Notes |
|---|---|---|---|---|
| 1 | `test_bldc_pwm` — PWM drive + direction control | Not started | — | Test all 4 gearbox ratios. Verify 3.3 V PWM accepted by driver. |
| 2 | `test_bldc_feedback` — Pulse counting + RPM | Not started | #1 | Sweep PWM duty cycles, log RPM vs duty. Find dead band. Needs 5 V → 3.3 V divider on CAP pin. |
| 3 | `test_ina_current` — INA240 current reading | Not started | #1 | Read analog output while motor runs at known duty cycles. Cross-check with external multimeter. |
| 4 | `test_rs485` — Echo / loopback with USB adapter | Not started | — | USB-C serial for commands, RS-485 for data path. Test both directions. |
| 5 | `test_oled` — SSD1306 display | Not started | — | I2C, verify address 0x3C. Display static text. |
| 6 | `test_buttons` — Resistor-ladder ADC read | Not started | — | Single ADC pin, 4 buttons. Verify voltage levels for each combination. |
| 7 | `test_camera` — OV3660 MJPEG over Wi-Fi | Not started | — | Dual-core: camera on Core 1. Test with browser and OpenCV client. |
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
