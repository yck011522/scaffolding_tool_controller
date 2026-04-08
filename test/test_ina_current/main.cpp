// test_ina_current — INA240 current sensor test
//
// Board: Waveshare ESP32-S3-Tiny
//
// Reads the INA240 analog output to measure current on the 24V rail
// while driving a BLDC motor at various duty cycles.
//
// Wiring (breadboard):
//   24V supply (+) ── shunt resistor ──┬── Motor Red (+24V)
//   INA240 IN+ ────────────────────────┘   (high side, before shunt)
//   INA240 IN- ────────────────────────┬── (after shunt, to motor)
//   24V supply (−) ── Motor Black ── GND (shared with ESP32 GND)
//
//   INA240 VS  → 3.3V (or 5V, check your board)
//   INA240 GND → GND
//   INA240 OUT → GPIO13 (ADC1 channel)
//
//   Motor Blue  (PWM) → GPIO6 (+ 4.7kΩ pull-down to GND)
//   Motor White (DIR) → GPIO5
//   Motor Yellow (CAP) → 22kΩ → GPIO7 → 47kΩ → GND (voltage divider)
//
//   ESP32 powered via USB-C
//
// This test drives Motor 1 (gripper) by default.
// Motor 2 (tightening) pins: DIR=GPIO9, PWM=GPIO10, CAP=GPIO11
//
// INA240 output voltage:
//   Vout = (I_load × R_shunt × Gain) + Vref
//   Vref is typically VS/2 for bidirectional or GND for unidirectional
//   Check your board's configuration.
//
// Serial commands (115200 baud, via USB-C):
//   PWM <0-100>     Set motor duty cycle
//   DIR <0|1>       Set direction
//   STOP            Set duty to 0%
//   STATUS          Print PWM, RPM, and current reading
//   READ            Single current reading (raw ADC + voltage + current mA)
//   LOG             Toggle periodic logging (every 500 ms)
//   SWEEP           Ramp 0→100% in 5% steps, log current at each step
//   SAMPLES <n>     Set number of ADC samples to average (default: 64)
//   RATIO <n>       Set gearbox ratio for RPM calculation (default: 56)

#include <Arduino.h>

// ── Pin definitions (Motor 1 — gripper) ─────────────────────────────
const int PIN_PWM = 6;      // GPIO6 — Blue wire — PWM speed command
const int PIN_DIR = 5;      // GPIO5 — White wire — direction control
const int PIN_CAP = 7;      // GPIO7 — Yellow wire — CAP pulse input
const int PIN_INA_OUT = 13; // GPIO13 — INA240 analog output → ADC1

// Motor 2 (tightening) pins — set to high-Z in this test
const int PIN_M2_DIR = 9;
const int PIN_M2_PWM = 10;
const int PIN_M2_CAP = 11;

// Other system pins — set to high-Z in this test
const int PIN_BTN1 = 1;
const int PIN_BTN2 = 2;
const int PIN_BTN3 = 3;
const int PIN_BTN4 = 4;
const int PIN_SDA = 15;
const int PIN_SCL = 16;
const int PIN_DE_RE = 18;

// ── PWM settings ────────────────────────────────────────────────────
const int PWM_CHANNEL = 0;
const int PWM_FREQ = 20000; // 20 kHz (chosen from feedback test)
const int PWM_RESOLUTION = 8;

// ── INA240 settings ─────────────────────────────────────────────────
const float INA_GAIN = 20.0;    // INA240A1 = ×20
const float SHUNT_MOHM = 100.0; // R100 = 0.1 Ω = 100 mΩ
const float SHUNT_OHM = SHUNT_MOHM / 1000.0;
int adcSamples = 64;         // ADC oversampling count
float currentOffsetMa = 0.0; // baseline offset from power-on calibration

// ── ADC calibration ─────────────────────────────────────────────────
// ESP32-S3 ADC: 12-bit (0–4095), default attenuation 11dB → ~0–3.1V
const float ADC_MAX = 4095.0;
const float ADC_VREF_MV = 3100.0; // approximate full-scale voltage in mV at 11dB atten

// ── Motor feedback ──────────────────────────────────────────────────
const int PULSES_PER_REV = 6;
float gearRatio = 56.0;

volatile unsigned long pulseCount = 0;
void IRAM_ATTR onCapPulse() { pulseCount++; }

unsigned long lastRpmTime = 0;
unsigned long lastPulseSnap = 0;
float motorRpm = 0.0;

// ── State ───────────────────────────────────────────────────────────
int currentDutyPercent = 0;
int currentDir = 0;
bool logEnabled = false;

// ── Current reading ─────────────────────────────────────────────────
struct CurrentReading
{
    int rawAdc;
    float voltageMv;
    float currentMa;
};

CurrentReading readCurrent()
{
    long sum = 0;
    for (int i = 0; i < adcSamples; i++)
    {
        sum += analogRead(PIN_INA_OUT);
    }
    int rawAdc = sum / adcSamples;
    float voltageMv = (rawAdc / ADC_MAX) * ADC_VREF_MV;

    // I = Vout / (Gain × Rshunt)
    // INA240A1 unidirectional: Vref = 0 (output referenced to GND)
    float currentMa = voltageMv / (INA_GAIN * SHUNT_OHM) - currentOffsetMa;

    return {rawAdc, voltageMv, currentMa};
}

// ── RPM ─────────────────────────────────────────────────────────────
void updateRpm()
{
    unsigned long now = millis();
    unsigned long elapsed = now - lastRpmTime;
    if (elapsed < 100)
        return;

    noInterrupts();
    unsigned long count = pulseCount;
    interrupts();

    unsigned long delta = count - lastPulseSnap;
    lastPulseSnap = count;
    lastRpmTime = now;

    float pulsesPerSec = (float)delta * 1000.0 / (float)elapsed;
    motorRpm = (pulsesPerSec / PULSES_PER_REV) * 60.0;
}

// ── Motor control ───────────────────────────────────────────────────
void setDuty(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    currentDutyPercent = percent;
    ledcWrite(PWM_CHANNEL, map(percent, 0, 100, 0, 255));
    Serial.print("PWM set to ");
    Serial.print(percent);
    Serial.println("%");
}

void setDir(int dir)
{
    currentDir = dir ? 1 : 0;
    digitalWrite(PIN_DIR, currentDir);
    Serial.print("DIR set to ");
    Serial.println(currentDir);
}

// ── Display ─────────────────────────────────────────────────────────
void printCurrentReading()
{
    CurrentReading r = readCurrent();
    Serial.print("ADC raw: ");
    Serial.print(r.rawAdc);
    Serial.print("  Voltage: ");
    Serial.print(r.voltageMv, 1);
    Serial.print(" mV  Current: ");
    Serial.print(r.currentMa, 1);
    Serial.println(" mA");
}

void printStatus()
{
    updateRpm();
    CurrentReading r = readCurrent();
    Serial.println("────────────────────────────");
    Serial.print("PWM: ");
    Serial.print(currentDutyPercent);
    Serial.println("%");
    Serial.print("DIR: ");
    Serial.println(currentDir);
    Serial.print("Motor RPM: ");
    Serial.println(motorRpm, 1);
    Serial.print("ADC raw: ");
    Serial.println(r.rawAdc);
    Serial.print("Voltage: ");
    Serial.print(r.voltageMv, 1);
    Serial.println(" mV");
    Serial.print("Current: ");
    Serial.print(r.currentMa, 1);
    Serial.println(" mA");
    Serial.println("INA240A1 ×20, shunt 100mΩ");
    Serial.println("────────────────────────────");
}

void printLogLine()
{
    updateRpm();
    CurrentReading r = readCurrent();
    Serial.print("LOG,");
    Serial.print(millis());
    Serial.print(",PWM%,");
    Serial.print(currentDutyPercent);
    Serial.print(",MotorRPM,");
    Serial.print(motorRpm, 1);
    Serial.print(",ADC,");
    Serial.print(r.rawAdc);
    Serial.print(",mV,");
    Serial.print(r.voltageMv, 1);
    Serial.print(",mA,");
    Serial.println(r.currentMa, 1);
}

void runSweep()
{
    Serial.println("SWEEP: PWM% → RPM, ADC, mV, mA (5% steps, 2s each)");
    Serial.println("step,PWM%,MotorRPM,ADC_raw,voltage_mV,current_mA");
    for (int pct = 0; pct <= 100; pct += 5)
    {
        setDuty(pct);
        delay(1500);

        // Reset RPM measurement window
        noInterrupts();
        lastPulseSnap = pulseCount;
        interrupts();
        lastRpmTime = millis();
        delay(500);

        updateRpm();
        CurrentReading r = readCurrent();
        Serial.print(pct / 5);
        Serial.print(",");
        Serial.print(pct);
        Serial.print(",");
        Serial.print(motorRpm, 1);
        Serial.print(",");
        Serial.print(r.rawAdc);
        Serial.print(",");
        Serial.print(r.voltageMv, 1);
        Serial.print(",");
        Serial.println(r.currentMa, 1);
    }
    Serial.println("Sweep complete. Use STOP to halt.");
}

// ── Command parsing ─────────────────────────────────────────────────
void processCommand(String cmd)
{
    cmd.trim();
    String upper = cmd;
    upper.toUpperCase();

    if (upper.startsWith("PWM "))
    {
        setDuty(upper.substring(4).toInt());
    }
    else if (upper.startsWith("DIR "))
    {
        setDir(upper.substring(4).toInt());
    }
    else if (upper == "SWEEP")
    {
        runSweep();
    }
    else if (upper == "STOP")
    {
        setDuty(0);
    }
    else if (upper == "STATUS")
    {
        printStatus();
    }
    else if (upper == "READ")
    {
        printCurrentReading();
    }
    else if (upper == "LOG")
    {
        logEnabled = !logEnabled;
        Serial.print("Periodic logging: ");
        Serial.println(logEnabled ? "ON" : "OFF");
    }
    else if (upper.startsWith("SAMPLES "))
    {
        adcSamples = upper.substring(8).toInt();
        if (adcSamples < 1)
            adcSamples = 1;
        Serial.print("ADC samples set to ");
        Serial.println(adcSamples);
    }
    else if (upper.startsWith("RATIO "))
    {
        gearRatio = cmd.substring(6).toFloat();
        Serial.print("Gear ratio set to ");
        Serial.println(gearRatio, 1);
    }
    else
    {
        Serial.println("Commands: PWM <0-100>, DIR <0|1>, SWEEP, STOP, STATUS, READ, LOG");
        Serial.println("Config:   SAMPLES <n>, RATIO <n>");
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
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);
    pinMode(PIN_DE_RE, INPUT);

    // Direction pin
    pinMode(PIN_DIR, OUTPUT);
    digitalWrite(PIN_DIR, LOW);

    // PWM
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(PIN_PWM, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);

    // CAP feedback
    pinMode(PIN_CAP, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_CAP), onCapPulse, RISING);

    // INA240 analog input
    pinMode(PIN_INA_OUT, INPUT);
    analogSetAttenuation(ADC_11db); // 0–3.1V range

    lastRpmTime = millis();

    delay(1000);
    Serial.println();
    Serial.println("=== test_ina_current ===");
    Serial.println("PWM: GPIO6, DIR: GPIO5, CAP: GPIO7, INA240: GPIO13 (Motor 1)");
    Serial.println("INA240A1 ×20, shunt 100mΩ (0.1Ω)");
    Serial.println();

    // ── Power-on current sensor calibration ──────────────────────────
    // Motor must be stationary at boot. Take multiple readings at 0% PWM
    // and average to determine the ADC/INA240 offset.
    Serial.print("[CAL] Calibrating current sensor");
    const int CAL_READINGS = 20;
    const int CAL_INTERVAL_MS = 50; // 20 × 50 ms = 1 s total
    float calSum = 0.0;
    for (int i = 0; i < CAL_READINGS; i++)
    {
        long adcSum = 0;
        for (int j = 0; j < adcSamples; j++)
        {
            adcSum += analogRead(PIN_INA_OUT);
        }
        float rawMv = ((float)(adcSum / adcSamples) / ADC_MAX) * ADC_VREF_MV;
        calSum += rawMv / (INA_GAIN * SHUNT_OHM);
        delay(CAL_INTERVAL_MS);
        Serial.print(".");
    }
    currentOffsetMa = calSum / CAL_READINGS;
    Serial.println(" done");
    Serial.print("[CAL] Baseline offset: ");
    Serial.print(currentOffsetMa, 1);
    Serial.println(" mA (will be subtracted from all readings)");
    Serial.println();

    Serial.println("Commands: PWM <0-100>, DIR <0|1>, SWEEP, STOP, STATUS, READ, LOG");
    Serial.println("Config:   SAMPLES <n>, RATIO <n>");
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

    if (logEnabled && (millis() - lastLogTime >= 500))
    {
        lastLogTime = millis();
        printLogLine();
    }
}
