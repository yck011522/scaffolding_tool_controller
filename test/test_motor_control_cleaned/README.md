# test_motor_control_cleaned — Where to tune what

Refactored split of `test_motor_control/` into focused modules. Same
behaviour, same tuned values — just reorganized so M1 and M2 are fully
parallel and every tunable lives in one obvious place.

## File map

| File | Owns |
|---|---|
| `motor_config.h` | `MotorConfig` and `Motor` struct definitions, action enum, `actionIdx()` |
| `motor_control.cpp` | `cfg[2]` initializer (all tunables), `motors[2]` runtime state, ISRs, current sensor, control loop, state machine |
| `motor_control.h` | API exposed to the rest of the program |
| `buttons.cpp` / `.h` | Debounced button input, edge-triggered start/stop |
| `display.cpp` / `.h` | SSD1306 OLED rendering |
| `serial_cmd.cpp` / `.h` | Serial command parser, `STATUS`, periodic log line |
| `main.cpp` | Glue: `setup()` and `loop()` |

## Where to tune what

### Per-motor tuning → `motor_control.cpp`, the `cfg[2]` initializer

Everything that can differ between M1 and M2 lives here, with both
motors visually side-by-side. Edit the field for the motor you care
about; leave the other alone.

| Field | Meaning |
|---|---|
| `pinPWM`, `pinDIR`, `pinCAP`, `pwmChannel` | Wiring (rarely changes) |
| `pulseCountPtr` | Address of that motor's ISR counter |
| `gearRatio` | Motor-to-shaft ratio (display only) |
| `pwmMin` | Dead-band floor (motor stalls below this) |
| `pwmMax` | Absolute hardware ceiling |
| `pwmStart[T,L]` | Startup kick PWM, per action |
| `pwmCeiling[T,L]` | Per-action output cap (use this to cap current spikes) |
| `slew[T,L]` | Max PWM step per control cycle, per action |
| `limitMa[T,L]` | Current-limit setpoint, per action |
| `kp`, `ki` | Velocity-form PI gains |
| `rampMs` | Soft-start ramp duration |

All of these are also reachable at runtime via serial commands —
see `serial_cmd.h` for the syntax (e.g. `LIMIT M1 T 300`,
`PWMMAX M2 L 200`, `KP M1 0.5`).

### Shared / hardware constants → `motor_control.cpp`, top of file

These are not per-motor and are unlikely to change during tuning.

| Constant | Meaning |
|---|---|
| `STALL_RPM_THRESHOLD`, `STALL_CONFIRM_MS`, `PULSE_TIMEOUT_MS`, `STARTUP_GRACE_MS` | Stall detection |
| `CONTROL_INTERVAL_MS` | Control loop period (100 Hz) |
| `PWM_FREQ`, `PWM_RESOLUTION` | LEDC hardware (file-scope `static`) |
| `INA_GAIN`, `SHUNT_OHM`, `ADC_SAMPLES`, `ADC_MAX`, `ADC_VREF_MV` | INA240 current sensor |
| `PIN_INA_OUT` | Current sensor ADC pin |
| `PULSES_PER_REV` | Encoder pulses per motor rev |

### Buttons → `buttons.cpp`, top of file

| Constant | Meaning |
|---|---|
| `BTN_PINS[4]` | GPIO pins for buttons 1–4 |
| `DEBOUNCE_MS` | Debounce window |
| `BTN_MOTOR[4]`, `BTN_ACTION[4]` | Which motor + action each button triggers |

### OLED display → `display.cpp`, top of file

| Constant | Meaning |
|---|---|
| `PIN_SDA`, `PIN_SCL` | I²C pins |
| `OLED_ADDR` | SSD1306 I²C address |
| `DISPLAY_INTERVAL_MS` | Refresh period (40 ms = 25 Hz) |

To change the layout, edit `updateDisplay()` in the same file.

## Runtime serial commands

See the comment block at the top of `serial_cmd.h`. All per-motor
parameters accept an optional `M1`/`M2` selector; omit to apply to both.
Range checks live inside `processCommand()` in `serial_cmd.cpp`.

## Build / upload / monitor

```powershell
cd <project_root>
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e test_motor_control_cleaned --target upload --upload-port COM22
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --port COM22 --baud 115200
```
