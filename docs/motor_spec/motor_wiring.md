# Motor Wiring

This motor/driver assembly is a 6-wire BLDC gearmotor with an integrated driver. The notes below summarize the manufacturer wiring examples and the current assumptions for this project.

## Cable Meanings

| Color | Function | Notes for this project |
| --- | --- | --- |
| Red | Motor supply positive | Connect to the positive 24 V supply. |
| Black | Motor supply negative | Connect to the 0 V / supply negative. |
| White | Direction control input | Controls CW/CCW direction. The exact logic level mapping for CW vs CCW should be verified in testing, because the manufacturer examples suggest that the active level may depend on the motor variant and rated direction in the gearbox table. |
| Blue | PWM speed command input | PWM input to the integrated motor driver. This is the speed-control signal we should drive from the controller.  |
| Yellow | CAP / capture / FG pulse output | Pulse output from the motor. The manufacturer notes 6 pulses per motor rotation, before the gearbox reduction. This should be read later with the ESP32 for speed/feedback measurement. |

## Project Notes

- Red is positive and black is negative.
- Use the blue wire as the PWM command input. When this pin is left unconnected (or connect this to 3.3V), the motor will spin at maximum speed. When this pin is connected to negative, motor will stop. 
- To ensure that the motor is not spinning when we start the entire tool or booting up the controller, we should have a pull-up resistor. A 10 kΩ pull-down resistor is sufficient to make the motor stop, but to play safe, maybe we should use a 4.7 kΩ. 
- Treat the white wire as the direction-select input, but verify in testing whether logic low or logic high corresponds to CW or CCW for the installed motor.
- The yellow wire is a pulse-output signal from the motor itself, not the gearbox output shaft. The documented pulse rate is 6 pulses per motor revolution.
- I have used a signal generator to monitor the capture (yellow) pin, and I found that the maximum voltage in the signal is around 5.2 V regardless of the frequency output. This means that we need a voltage divider before we give this to our ESP32. 
- For the 24 V, 142 rpm motor running at full speed, the CAP frequency is 830 Hz. When the blue cable is provided with 2.5 VDC over 3.3 V, the frequency is 600 Hz. When blue cable is provided with 1.5 VDC (very slow speed), the frequency is 140 Hz. The motor stops at around 1.2 VDC, CAP frequency drops to zero and the pin is held at 5.2V. PWM input may differ.
