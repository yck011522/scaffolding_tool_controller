// test_bldc_feedback — BLDC motor PWM drive + CAP pulse feedback test
//
// Board: Waveshare ESP32-S3-Tiny
//
// Wiring (breadboard):
//   Motor Red   → +24V supply
//   Motor Black → GND (shared with ESP32 GND)
//   Motor Blue  (PWM in) → GPIO6  (+ 4.7kΩ pull-DOWN to GND for boot safety)
//   Motor White (DIR)    → GPIO5
//   Motor Yellow (CAP)   → 22kΩ → GPIO7  (voltage divider: 5.2V → ~2.97V)
//                           GPIO7 → 47kΩ → GND
//   ESP32 powered via USB-C
//
// This test drives Motor 1 (gripper) by default.
// Motor 2 (tightening) pins: DIR=GPIO9, PWM=GPIO10, CAP=GPIO11
//
// IMPORTANT: The 24V supply GND and the ESP32 GND must be connected together.
//
// CAP voltage divider (high-impedance source):
//   Yellow ──[22kΩ]──┬── GPIO7 (input + interrupt)
//                     │
//                  [47kΩ]
//                     │
//                    GND
//   Output: ~2.97V measured (safe for ESP32, above HIGH threshold)
//
// Motor feedback: 6 pulses per motor revolution (before gearbox).
//   Output shaft RPM = (pulse_freq / 6) × 60 / gear_ratio
//
// Serial commands (115200 baud, via USB-C):
//   PWM <0-100>    Set duty cycle percentage
//   DIR <0|1>      Set direction pin
//   STOP           Set duty to 0%
//   STATUS         Print current settings and RPM
//   FREQ <hz>      Change PWM frequency
//   LOG            Toggle periodic RPM logging (every 500 ms)
//   RATIO <n>      Set gearbox ratio for RPM calculation (default: 56)
//   SWEEP          Ramp 0→100% in 5% steps, 2s each, logging RPM at each step

#include <Arduino.h>

// ── Pin definitions (Motor 1 — gripper) ─────────────────────────────
const int PIN_PWM = 6; // GPIO6 — Blue wire — PWM speed command
const int PIN_DIR = 5; // GPIO5 — White wire — direction control
const int PIN_CAP = 7; // GPIO7 — Yellow wire — CAP pulse input (via voltage divider)

// Motor 2 (tightening) pins — set to high-Z in this test
const int PIN_M2_DIR = 9;
const int PIN_M2_PWM = 10;
const int PIN_M2_CAP = 11;

// Other system pins — set to high-Z in this test
const int PIN_BTN1 = 1;
const int PIN_BTN2 = 2;
const int PIN_BTN3 = 3;
const int PIN_BTN4 = 4;
const int PIN_INA_OUT = 13;
const int PIN_SDA = 15;
const int PIN_SCL = 16;
const int PIN_DE_RE = 18;

// ── PWM settings ────────────────────────────────────────────────────
const int PWM_CHANNEL = 0;
int pwmFreq = 5000;
const int PWM_RESOLUTION = 8;

// ── Feedback settings ───────────────────────────────────────────────
const int PULSES_PER_REV = 6; // 6 pulses per motor revolution
float gearRatio = 56.0;       // gearbox ratio (motor revs per output rev)

// ── State ───────────────────────────────────────────────────────────
int currentDutyPercent = 0;
int currentDir = 0;
bool logEnabled = false;

// ── Pulse counting (ISR-safe) ───────────────────────────────────────
volatile unsigned long pulseCount = 0;

void IRAM_ATTR onCapPulse()
{
    pulseCount++;
}

// ── RPM calculation ─────────────────────────────────────────────────
unsigned long lastRpmTime = 0;
unsigned long lastPulseSnap = 0;
float motorRpm = 0.0;
float shaftRpm = 0.0;

void updateRpm()
{
    unsigned long now = millis();
    unsigned long elapsed = now - lastRpmTime;
    if (elapsed < 100)
        return; // update at most every 100 ms

    noInterrupts();
    unsigned long count = pulseCount;
    interrupts();

    unsigned long delta = count - lastPulseSnap;
    lastPulseSnap = count;
    lastRpmTime = now;

    // pulses/ms → pulses/min, then ÷ 6 = motor revs/min
    float pulsesPerSec = (float)delta * 1000.0 / (float)elapsed;
    motorRpm = (pulsesPerSec / PULSES_PER_REV) * 60.0;
    shaftRpm = motorRpm / gearRatio;
}

// ── Motor control ───────────────────────────────────────────────────
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
    updateRpm();
    Serial.println("────────────────────────────");
    Serial.print("PWM: ");
    Serial.print(currentDutyPercent);
    Serial.println("%");
    Serial.print("DIR: ");
    Serial.println(currentDir);
    Serial.print("Freq: ");
    Serial.print(pwmFreq);
    Serial.println(" Hz");
    Serial.print("Gear ratio: ");
    Serial.println(gearRatio, 1);
    Serial.print("Motor RPM: ");
    Serial.println(motorRpm, 1);
    Serial.print("Shaft RPM: ");
    Serial.println(shaftRpm, 1);

    noInterrupts();
    unsigned long total = pulseCount;
    interrupts();
    Serial.print("Total pulses: ");
    Serial.println(total);
    Serial.println("────────────────────────────");
}

void printLogLine()
{
    updateRpm();
    // Compact CSV-style output for easy copy/paste
    Serial.print("LOG,");
    Serial.print(millis());
    Serial.print(",PWM%,");
    Serial.print(currentDutyPercent);
    Serial.print(",MotorRPM,");
    Serial.print(motorRpm, 1);
    Serial.print(",ShaftRPM,");
    Serial.println(shaftRpm, 1);
}

void runSweep()
{
    Serial.println("SWEEP: PWM% → MotorRPM, ShaftRPM (5% steps, 2s each)");
    Serial.println("step,PWM%,MotorRPM,ShaftRPM");
    for (int pct = 0; pct <= 100; pct += 5)
    {
        setDuty(pct);
        // Wait 1.5s for motor to stabilize, then measure over last 0.5s
        delay(1500);
        // Reset measurement window
        noInterrupts();
        lastPulseSnap = pulseCount;
        interrupts();
        lastRpmTime = millis();
        delay(500);
        updateRpm();
        Serial.print(pct / 5);
        Serial.print(",");
        Serial.print(pct);
        Serial.print(",");
        Serial.print(motorRpm, 1);
        Serial.print(",");
        Serial.println(shaftRpm, 1);
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
        setDuty(cmd.substring(4).toInt());
    }
    else if (cmd.startsWith("DIR "))
    {
        setDir(cmd.substring(4).toInt());
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
    else if (cmd == "LOG")
    {
        logEnabled = !logEnabled;
        Serial.print("Periodic logging: ");
        Serial.println(logEnabled ? "ON (500ms)" : "OFF");
    }
    else if (cmd.startsWith("RATIO "))
    {
        gearRatio = cmd.substring(6).toFloat();
        Serial.print("Gear ratio set to ");
        Serial.println(gearRatio, 1);
    }
    else if (cmd.startsWith("FREQ "))
    {
        pwmFreq = cmd.substring(5).toInt();
        ledcSetup(PWM_CHANNEL, pwmFreq, PWM_RESOLUTION);
        ledcAttachPin(PIN_PWM, PWM_CHANNEL);
        int dutyValue = map(currentDutyPercent, 0, 100, 0, 255);
        ledcWrite(PWM_CHANNEL, dutyValue);
        Serial.print("PWM frequency changed to ");
        Serial.print(pwmFreq);
        Serial.println(" Hz");
    }
    else
    {
        Serial.println("Commands: PWM <0-100>, DIR <0|1>, SWEEP, STOP, STATUS, LOG, RATIO <n>, FREQ <hz>");
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
    pinMode(PIN_M2_DIR, INPUT);
    pinMode(PIN_M2_PWM, INPUT);
    pinMode(PIN_M2_CAP, INPUT);
    pinMode(PIN_INA_OUT, INPUT);
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);
    pinMode(PIN_DE_RE, INPUT);

    // Direction pin
    pinMode(PIN_DIR, OUTPUT);
    digitalWrite(PIN_DIR, LOW);

    // PWM pin
    ledcSetup(PWM_CHANNEL, pwmFreq, PWM_RESOLUTION);
    ledcAttachPin(PIN_PWM, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);

    // CAP feedback pin — interrupt on rising edge
    pinMode(PIN_CAP, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_CAP), onCapPulse, RISING);

    lastRpmTime = millis();

    delay(1000);
    Serial.println();
    Serial.println("=== test_bldc_feedback ===");
    Serial.println("PWM: GPIO6, DIR: GPIO5, CAP: GPIO7 (Motor 1 — gripper)");
    Serial.print("PWM freq: ");
    Serial.print(pwmFreq);
    Serial.println(" Hz");
    Serial.print("Gear ratio: ");
    Serial.println(gearRatio, 1);
    Serial.println("6 pulses/motor-rev");
    Serial.println();
    Serial.println("Commands: PWM <0-100>, DIR <0|1>, SWEEP, STOP, STATUS, LOG, RATIO <n>, FREQ <hz>");
    Serial.println();
}

unsigned long lastLogTime = 0;

void loop()
{
    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }

    // Periodic logging
    if (logEnabled && (millis() - lastLogTime >= 500))
    {
        lastLogTime = millis();
        printLogLine();
    }
}
