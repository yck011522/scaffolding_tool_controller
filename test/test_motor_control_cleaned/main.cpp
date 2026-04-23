// main.cpp — test_motor_control_cleaned
//
// Glue file. All logic lives in the focused modules:
//   motor_control.{h,cpp}  — cfg[], motors[], control loop, state machine
//   buttons.{h,cpp}        — debounced input
//   display.{h,cpp}        — OLED rendering
//   serial_cmd.{h,cpp}     — command parser, status, periodic log
//
// Hardware: Waveshare ESP32-S3-Tiny. See README.md in this folder for
// where to tune what.

#include <Arduino.h>

#include "motor_control.h"
#include "buttons.h"
#include "display.h"
#include "serial_cmd.h"

// RS-485 driver enable (unused in this test — keep high-Z).
static const int PIN_DE_RE = 18;

// Last time printLogLine() ran (for the LOG-mode 200 ms cadence).
static unsigned long lastLogTime = 0;

void setup()
{
    Serial.begin(115200);

    // RS-485 DE/RE not driven — leave the line undriven.
    pinMode(PIN_DE_RE, INPUT);

    // Bring up subsystems in dependency order:
    //   display first (boot splash before anything slow happens),
    //   then buttons (cheap), then motors (configures LEDC + ISRs).
    setupDisplay();
    displayCalibrating();

    setupButtons();
    setupMotors();

    // Give USB-CDC a moment to enumerate so the banner isn't lost.
    delay(1000);
    Serial.println();
    Serial.println("=== test_motor_control_cleaned ===");
    Serial.println("Current-limited motor control with stall detection");
    Serial.println("M1: PWM=GPIO6 DIR=GPIO5 CAP=GPIO7");
    Serial.println("M2: PWM=GPIO10 DIR=GPIO9 CAP=GPIO11");
    Serial.println("INA240: GPIO13 (shared 24V rail)");
    Serial.println();

    // Quiescent INA240 reading → currentOffsetMa. Motors must be off here.
    calibrateCurrentSensor();
    Serial.println();

    printHelp();
    Serial.println();
    printStatus();
}

void loop()
{
    // 1. Sample buttons and act on press/release edges.
    updateButtons();
    processButtonEvents();

    // 2. Run the PI loop for whichever motor is currently active. The
    //    loop is internally throttled to CONTROL_INTERVAL_MS, so it's
    //    safe to call every iteration.
    if (activeMotor >= 0)
        controlLoop(activeMotor);

    // 3. Refresh the OLED (throttled internally to ~25 Hz).
    updateDisplay();

    // 4. Drain one serial line per loop iteration if available.
    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }

    // 5. Periodic CSV log line when the LOG command is enabled.
    if (logEnabled && (millis() - lastLogTime >= 200))
    {
        lastLogTime = millis();
        printLogLine();
    }
}
