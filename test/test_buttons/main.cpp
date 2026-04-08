// test_buttons — Momentary button input test
//
// Board: Waveshare ESP32-S3-Tiny
//
// Tests 4 momentary push buttons connected to individual GPIO pins.
// Each button has an internal pull-up resistor and is active LOW
// (pressing the button pulls the pin to GND).
//
// Wiring:
//   Button 1 → GPIO1 ←→ GND (momentary, normally open)
//   Button 2 → GPIO2 ←→ GND
//   Button 3 → GPIO3 ←→ GND
//   Button 4 → GPIO4 ←→ GND
//   ESP32 powered via USB-C
//
// Serial commands (115200 baud, via USB-C):
//   STATUS    Print current state of all 4 buttons
//   LOG       Toggle periodic logging (every 200 ms)
//   WAIT      Block until any button is pressed, then report which one

#include <Arduino.h>

// ── Pin definitions ─────────────────────────────────────────────────
const int PIN_BTN1 = 1; // GPIO1 — Button 1
const int PIN_BTN2 = 2; // GPIO2 — Button 2
const int PIN_BTN3 = 3; // GPIO3 — Button 3
const int PIN_BTN4 = 4; // GPIO4 — Button 4

const int NUM_BUTTONS = 4;
const int btnPins[NUM_BUTTONS] = {PIN_BTN1, PIN_BTN2, PIN_BTN3, PIN_BTN4};

// Other system pins — set to high-Z in this test
const int PIN_M1_DIR = 5;
const int PIN_M1_PWM = 6;
const int PIN_M1_CAP = 7;
const int PIN_M2_DIR = 9;
const int PIN_M2_PWM = 10;
const int PIN_M2_CAP = 11;
const int PIN_INA_OUT = 13;
const int PIN_SDA = 15;
const int PIN_SCL = 16;
const int PIN_DE_RE = 18;

// ── Debounce settings ───────────────────────────────────────────────
const unsigned long DEBOUNCE_MS = 50;
unsigned long lastDebounce[NUM_BUTTONS] = {0, 0, 0, 0};
bool btnState[NUM_BUTTONS] = {false, false, false, false}; // true = pressed
bool lastReading[NUM_BUTTONS] = {false, false, false, false};

// ── State ───────────────────────────────────────────────────────────
bool logEnabled = false;

// ── Button reading with debounce ────────────────────────────────────
void updateButtons()
{
    unsigned long now = millis();
    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        bool reading = (digitalRead(btnPins[i]) == LOW); // active LOW
        if (reading != lastReading[i])
        {
            lastDebounce[i] = now;
        }
        if ((now - lastDebounce[i]) > DEBOUNCE_MS)
        {
            if (reading != btnState[i])
            {
                btnState[i] = reading;
                // Print event on state change
                Serial.print("[BTN] Button ");
                Serial.print(i + 1);
                Serial.print(" (GPIO");
                Serial.print(btnPins[i]);
                Serial.print("): ");
                Serial.println(btnState[i] ? "PRESSED" : "RELEASED");
            }
        }
        lastReading[i] = reading;
    }
}

void printStatus()
{
    Serial.println("────────────────────────────");
    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        Serial.print("Button ");
        Serial.print(i + 1);
        Serial.print(" (GPIO");
        Serial.print(btnPins[i]);
        Serial.print("): ");
        Serial.print(btnState[i] ? "PRESSED" : "released");
        Serial.print("  (raw=");
        Serial.print(digitalRead(btnPins[i]));
        Serial.println(")");
    }
    Serial.println("────────────────────────────");
}

void printLogLine()
{
    Serial.print("LOG,");
    Serial.print(millis());
    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        Serial.print(",btn");
        Serial.print(i + 1);
        Serial.print(",");
        Serial.print(btnState[i] ? 1 : 0);
    }
    Serial.println();
}

void waitForPress()
{
    Serial.println("Waiting for any button press...");
    // Wait until all buttons are released first
    bool allReleased = false;
    while (!allReleased)
    {
        allReleased = true;
        for (int i = 0; i < NUM_BUTTONS; i++)
        {
            if (digitalRead(btnPins[i]) == LOW)
            {
                allReleased = false;
            }
        }
        delay(10);
    }
    // Now wait for a press
    while (true)
    {
        for (int i = 0; i < NUM_BUTTONS; i++)
        {
            if (digitalRead(btnPins[i]) == LOW)
            {
                delay(DEBOUNCE_MS); // debounce
                if (digitalRead(btnPins[i]) == LOW)
                {
                    Serial.print("[WAIT] Button ");
                    Serial.print(i + 1);
                    Serial.print(" (GPIO");
                    Serial.print(btnPins[i]);
                    Serial.println(") pressed");
                    return;
                }
            }
        }
        delay(5);
    }
}

// ── Command parsing ─────────────────────────────────────────────────
void processCommand(String cmd)
{
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "STATUS")
    {
        printStatus();
    }
    else if (cmd == "LOG")
    {
        logEnabled = !logEnabled;
        Serial.print("Periodic logging: ");
        Serial.println(logEnabled ? "ON (200ms)" : "OFF");
    }
    else if (cmd == "WAIT")
    {
        waitForPress();
    }
    else
    {
        Serial.println("Commands: STATUS, LOG, WAIT");
    }
}

// ── Setup & Loop ────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);

    // ── Set all non-tested pins to high-Z (INPUT) ────────────────────
    pinMode(PIN_M1_DIR, INPUT);
    pinMode(PIN_M1_PWM, INPUT);
    pinMode(PIN_M1_CAP, INPUT);
    pinMode(PIN_M2_DIR, INPUT);
    pinMode(PIN_M2_PWM, INPUT);
    pinMode(PIN_M2_CAP, INPUT);
    pinMode(PIN_INA_OUT, INPUT);
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);
    pinMode(PIN_DE_RE, INPUT);

    // Configure button pins with internal pull-up
    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        pinMode(btnPins[i], INPUT_PULLUP);
    }

    delay(1000);
    Serial.println();
    Serial.println("=== test_buttons ===");
    Serial.println("4 buttons on GPIO1-4 (pull-up, active LOW)");
    Serial.println("Press events are printed automatically.");
    Serial.println();
    Serial.println("Commands: STATUS, LOG, WAIT");
    Serial.println();

    // Print initial state
    printStatus();
}

unsigned long lastLogTime = 0;

void loop()
{
    // Always update button state (debounced, prints events)
    updateButtons();

    // Process serial commands
    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }

    // Periodic logging
    if (logEnabled && (millis() - lastLogTime >= 200))
    {
        lastLogTime = millis();
        printLogLine();
    }
}
