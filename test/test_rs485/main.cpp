// test_rs485 — RS-485 half-duplex communication test
//
// Board: Waveshare ESP32-S3-Tiny
//
// Verifies bidirectional RS-485 communication via MAX3485 transceiver.
// USB-C Serial (USB CDC) is used for commands and debug output.
// Serial1 is used for the RS-485 data path on the dedicated UART0 TX/RX pins.
//
// Wiring:
//   GPIO43 (TX, board header) → MAX3485 DI (driver input)
//   GPIO44 (RX, board header) → MAX3485 RO (receiver output)
//   GPIO18                    → MAX3485 DE + RE (tied together, direction control)
//   MAX3485 VCC → 3.3 V
//   MAX3485 GND → GND
//   MAX3485 A/B → RS-485 bus (to USB-RS485 adapter on PC)
//
// ⚠ Module label confusion:
//   The pin labeled "TXD" on the MAX3485 module is RO (receiver output)
//   → connect to MCU RX (GPIO44).
//   The pin labeled "RXD" on the module is DI (driver input)
//   → connect to MCU TX (GPIO43).
//   In short: module TXD → MCU RX, module RXD → MCU TX.
//
// Direction control:
//   DE/RE HIGH → transmit mode (DI drives A/B)
//   DE/RE LOW  → receive mode  (A/B drives RO)
//
// Serial commands (115200 baud, via USB-C):
//   SEND <message>   Send a message on RS-485
//   ECHO             Toggle echo mode (RS-485 RX → RS-485 TX)
//   BAUD <rate>      Change RS-485 baud rate (default: 115200)
//   PING             Send "PING" on RS-485, expect "PONG" reply
//   STATUS           Print current config
//
// Any data received on RS-485 is printed on USB-C Serial for monitoring.

#include <Arduino.h>

// ── Pin definitions ─────────────────────────────────────────────────
const int PIN_RS485_TX = 43; // GPIO43 (TX header) → MAX3485 DI
const int PIN_RS485_RX = 44; // GPIO44 (RX header) → MAX3485 RO
const int PIN_DE_RE = 18;    // GPIO18 → MAX3485 DE + ~RE

// Other system pins — set to high-Z in this test
const int PIN_BTN1 = 1;
const int PIN_BTN2 = 2;
const int PIN_BTN3 = 3;
const int PIN_BTN4 = 4;
const int PIN_M1_DIR = 5;
const int PIN_M1_PWM = 6;
const int PIN_M1_CAP = 7;
const int PIN_M2_DIR = 9;
const int PIN_M2_PWM = 10;
const int PIN_M2_CAP = 11;
const int PIN_INA_OUT = 13;
const int PIN_SDA = 15;
const int PIN_SCL = 16;

// ── RS-485 settings ─────────────────────────────────────────────────
unsigned long rs485Baud = 115200;
bool echoEnabled = true;

// ── Direction control ───────────────────────────────────────────────
void rs485Transmit()
{
    digitalWrite(PIN_DE_RE, HIGH);
    delayMicroseconds(10); // allow transceiver to settle
}

void rs485Receive()
{
    delayMicroseconds(10);
    digitalWrite(PIN_DE_RE, LOW);
}

// ── RS-485 send ─────────────────────────────────────────────────────
void rs485Send(const char *msg)
{
    rs485Transmit();
    Serial1.print(msg);
    Serial1.print("\r\n");
    Serial1.flush(); // wait for TX buffer to drain before switching direction
    rs485Receive();
}

void rs485SendRaw(const uint8_t *data, size_t len)
{
    rs485Transmit();
    Serial1.write(data, len);
    Serial1.flush();
    rs485Receive();
}

// ── Command parsing ─────────────────────────────────────────────────
void processCommand(String cmd)
{
    cmd.trim();
    String upper = cmd;
    upper.toUpperCase();

    if (upper.startsWith("SEND "))
    {
        String msg = cmd.substring(5);
        Serial.print("[TX] ");
        Serial.println(msg);
        rs485Send(msg.c_str());
    }
    else if (upper == "ECHO")
    {
        echoEnabled = !echoEnabled;
        Serial.print("Echo mode: ");
        Serial.println(echoEnabled ? "ON" : "OFF");
    }
    else if (upper.startsWith("BAUD "))
    {
        rs485Baud = upper.substring(5).toInt();
        if (rs485Baud < 300)
            rs485Baud = 115200;
        Serial1.end();
        Serial1.begin(rs485Baud, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
        Serial.print("RS-485 baud rate set to ");
        Serial.println(rs485Baud);
    }
    else if (upper == "PING")
    {
        Serial.println("[TX] PING");
        rs485Send("PING");
        // Wait for reply
        unsigned long start = millis();
        String reply = "";
        while (millis() - start < 2000)
        {
            if (Serial1.available())
            {
                char c = Serial1.read();
                if (c == '\n' || c == '\r')
                {
                    if (reply.length() > 0)
                        break;
                }
                else
                {
                    reply += c;
                }
            }
        }
        if (reply.length() > 0)
        {
            Serial.print("[RX] ");
            Serial.print(reply);
            Serial.print("  (");
            Serial.print(millis() - start);
            Serial.println(" ms)");
        }
        else
        {
            Serial.println("[RX] No reply (timeout 2s)");
        }
    }
    else if (upper == "STATUS")
    {
        Serial.println("────────────────────────────");
        Serial.print("RS-485 baud: ");
        Serial.println(rs485Baud);
        Serial.print("Echo mode:   ");
        Serial.println(echoEnabled ? "ON" : "OFF");
        Serial.print("TX pin:      GPIO");
        Serial.println(PIN_RS485_TX);
        Serial.print("RX pin:      GPIO");
        Serial.println(PIN_RS485_RX);
        Serial.print("DE/RE pin:   GPIO");
        Serial.println(PIN_DE_RE);
        Serial.println("────────────────────────────");
    }
    else
    {
        Serial.println("Commands: SEND <msg>, ECHO, PING, BAUD <rate>, STATUS");
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
    pinMode(PIN_M1_DIR, INPUT);
    pinMode(PIN_M1_PWM, INPUT);
    pinMode(PIN_M1_CAP, INPUT);
    pinMode(PIN_M2_DIR, INPUT);
    pinMode(PIN_M2_PWM, INPUT);
    pinMode(PIN_M2_CAP, INPUT);
    pinMode(PIN_INA_OUT, INPUT);
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);

    // DE/RE direction control — start in receive mode
    pinMode(PIN_DE_RE, OUTPUT);
    digitalWrite(PIN_DE_RE, LOW);

    // RS-485 UART
    Serial1.begin(rs485Baud, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);

    delay(1000);
    Serial.println();
    Serial.println("=== test_rs485 ===");
    Serial.println("TX: GPIO43, RX: GPIO44, DE/RE: GPIO18");
    Serial.print("Baud: ");
    Serial.println(rs485Baud);
    Serial.println();
    Serial.println("Commands: SEND <msg>, ECHO, PING, BAUD <rate>, STATUS");
    Serial.println();
}

void loop()
{
    // Process USB-C serial commands
    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }

    // Process incoming RS-485 data (line-terminated)
    static String rs485Buffer = "";
    while (Serial1.available())
    {
        char c = Serial1.read();
        if (c == '\n' || c == '\r')
        {
            if (rs485Buffer.length() > 0)
            {
                Serial.print("[RX] ");
                Serial.println(rs485Buffer);

                // Echo back if enabled
                if (echoEnabled)
                {
                    Serial.print("[ECHO TX] ");
                    Serial.println(rs485Buffer);
                    rs485Send(rs485Buffer.c_str());
                }
                rs485Buffer = "";
            }
        }
        else
        {
            rs485Buffer += c;
        }
    }
}
