// test_bldc_pwm — BLDC motor PWM drive + direction test
//
// Board: Waveshare ESP32-S3-Tiny
//
// Wiring (breadboard):
//   Motor Red   → +24V supply
//   Motor Black → GND (shared with ESP32 GND)
//   Motor Blue  (PWM in) → GPIO6  (+ 4.7kΩ pull-DOWN to GND for boot safety)
//   Motor White (DIR)    → GPIO5
//   Motor Yellow (CAP)   → disconnected
//   ESP32 powered via USB-C
//
// This test drives Motor 1 (gripper) by default.
// Motor 2 (tightening) pins: DIR=GPIO9, PWM=GPIO10, CAP=GPIO11
//
// IMPORTANT: The 24V supply GND and the ESP32 GND must be connected together.
//
// Serial commands (115200 baud, via USB-C):
//   PWM <0-100>    Set duty cycle percentage (0 = stopped, 100 = full speed)
//   DIR <0|1>      Set direction pin (0 = LOW, 1 = HIGH)
//   SWEEP           Ramp from 0% to 100% in 5% steps, 2 seconds each
//   STOP            Set duty to 0%
//   STATUS          Print current PWM and direction settings
//   FREQ <hz>       Change PWM frequency live (e.g., FREQ 20000)

#include <Arduino.h>

// ── Pin definitions (Motor 1 — gripper) ─────────────────────────────
const int PIN_PWM = 6; // GPIO6 — Blue wire — PWM speed command
const int PIN_DIR = 5; // GPIO5 — White wire — direction control

// Motor 2 (tightening) pins — active only in motor 2 tests
const int PIN_M2_DIR = 9;
const int PIN_M2_PWM = 10;
const int PIN_M2_CAP = 11;

// Other system pins — set to high-Z in this test
const int PIN_BTN1 = 1;
const int PIN_BTN2 = 2;
const int PIN_BTN3 = 3;
const int PIN_BTN4 = 4;
const int PIN_M1_CAP = 7;
const int PIN_INA_OUT = 13;
const int PIN_SDA = 15;
const int PIN_SCL = 16;
const int PIN_DE_RE = 18;

// ── PWM settings ────────────────────────────────────────────────────
const int PWM_CHANNEL = 0;    // LEDC channel (0–15)
int pwmFreq = 5000;           // Hz — tunable at runtime via FREQ command
const int PWM_RESOLUTION = 8; // 8-bit → duty values 0–255

// ── State ───────────────────────────────────────────────────────────
int currentDutyPercent = 0;
int currentDir = 0;

// ── Helpers ─────────────────────────────────────────────────────────
void setDuty(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    currentDutyPercent = percent;

    int dutyValue = map(percent, 0, 100, 0, 255);
    ledcWrite(PWM_CHANNEL, dutyValue);

    Serial.print("PWM set to ");
    Serial.print(percent);
    Serial.print("% (duty=");
    Serial.print(dutyValue);
    Serial.println("/255)");
}

void setDir(int dir)
{
    currentDir = dir ? 1 : 0;
    digitalWrite(PIN_DIR, currentDir);

    Serial.print("DIR set to ");
    Serial.println(currentDir);
}

void printStatus()
{
    Serial.println("────────────────────────────");
    Serial.print("PWM: ");
    Serial.print(currentDutyPercent);
    Serial.println("%");
    Serial.print("DIR: ");
    Serial.println(currentDir);
    Serial.print("Freq: ");
    Serial.print(pwmFreq);
    Serial.println(" Hz");
    Serial.println("Dead band: ~37-40%");
    Serial.println("────────────────────────────");
}

void runSweep()
{
    Serial.println("Starting PWM sweep 0% → 100% (5% steps, 2s each)...");
    for (int pct = 0; pct <= 100; pct += 5)
    {
        setDuty(pct);
        delay(2000);
    }
    Serial.println("Sweep complete. Motor at 100%. Use STOP to halt.");
}

// ── Command parsing ─────────────────────────────────────────────────
void processCommand(String cmd)
{
    cmd.trim();
    cmd.toUpperCase();

    if (cmd.startsWith("PWM "))
    {
        int val = cmd.substring(4).toInt();
        setDuty(val);
    }
    else if (cmd.startsWith("DIR "))
    {
        int val = cmd.substring(4).toInt();
        setDir(val);
    }
    else if (cmd == "SWEEP")
    {
        runSweep();
    }
    else if (cmd == "STOP")
    {
        setDuty(0);
    }
    else if (cmd == "STATUS")
    {
        printStatus();
    }
    else if (cmd.startsWith("FREQ "))
    {
        pwmFreq = cmd.substring(5).toInt();
        ledcSetup(PWM_CHANNEL, pwmFreq, PWM_RESOLUTION);
        ledcAttachPin(PIN_PWM, PWM_CHANNEL);
        // Restore current duty after frequency change
        int dutyValue = map(currentDutyPercent, 0, 100, 0, 255);
        ledcWrite(PWM_CHANNEL, dutyValue);
        Serial.print("PWM frequency changed to ");
        Serial.print(pwmFreq);
        Serial.println(" Hz");
    }
    else
    {
        Serial.println("Commands: PWM <0-100>, DIR <0|1>, SWEEP, STOP, STATUS, FREQ <hz>");
    }
}

// ── Setup & Loop ────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);

    // ── Set all non-tested pins to high-Z (INPUT) ────────────────────
    pinMode(PIN_BTN1, INPUT);
    pinMode(PIN_BTN2, INPUT);
    pinMode(PIN_BTN3, INPUT);
    pinMode(PIN_BTN4, INPUT);
    pinMode(PIN_M1_CAP, INPUT);
    pinMode(PIN_M2_DIR, INPUT);
    pinMode(PIN_M2_PWM, INPUT);
    pinMode(PIN_M2_CAP, INPUT);
    pinMode(PIN_INA_OUT, INPUT);
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);
    pinMode(PIN_DE_RE, INPUT);

    // Direction pin — start LOW
    pinMode(PIN_DIR, OUTPUT);
    digitalWrite(PIN_DIR, LOW);

    // PWM pin — configure LEDC channel, attach to pin, start at 0% duty
    ledcSetup(PWM_CHANNEL, pwmFreq, PWM_RESOLUTION);
    ledcAttachPin(PIN_PWM, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);

    delay(1000);
    Serial.println();
    Serial.println("=== test_bldc_pwm ===");
    Serial.println("PWM on GPIO6, DIR on GPIO5 (Motor 1 — gripper)");
    Serial.print("PWM frequency: ");
    Serial.print(pwmFreq);
    Serial.println(" Hz");
    Serial.println();
    Serial.println("Commands: PWM <0-100>, DIR <0|1>, SWEEP, STOP, STATUS, FREQ <hz>");
    Serial.println();
}

void loop()
{
    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }
}
