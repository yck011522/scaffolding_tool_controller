// serial_cmd.cpp — see serial_cmd.h
//
// All commands are line-based. The parser uppercases the line, peels off
// optional "M1"/"M2" and "T"/"L" prefixes, then dispatches. Per-motor
// commands without a motor selector apply to BOTH motors so a single
// keystroke (e.g. `LIMIT 400`) affects everything.

#include "serial_cmd.h"
#include "motor_control.h"
#include "buttons.h"

bool logEnabled = false;

// ── Helpers ─────────────────────────────────────────────────────────
// Strip a leading "M1 " / "M2 " token. Mutates `arg` in-place.
// Returns motor index (0/1) or -1 for "both".
static int parseMotorPrefix(String &arg)
{
    arg.trim();
    if (arg.startsWith("M1 ")) { arg = arg.substring(3); arg.trim(); return 0; }
    if (arg.startsWith("M2 ")) { arg = arg.substring(3); arg.trim(); return 1; }
    return -1;
}

// Strip a leading "T " / "L " token. Returns 0 (T), 1 (L), or -1 (both).
static int parseActionPrefix(String &arg)
{
    arg.trim();
    if (arg.startsWith("T ")) { arg = arg.substring(2); arg.trim(); return 0; }
    if (arg.startsWith("L ")) { arg = arg.substring(2); arg.trim(); return 1; }
    return -1;
}

// Apply `op(motorIdx)` to one or both motors based on mIdx (-1 = both).
// Templated so callers can pass a lambda capturing local state.
template <typename Fn>
static void forMotors(int mIdx, Fn op)
{
    int s = (mIdx >= 0) ? mIdx : 0;
    int e = (mIdx >= 0) ? mIdx : NUM_MOTORS - 1;
    for (int i = s; i <= e; i++) op(i);
}

// If a motor is currently RUNNING, refresh its activeLimitMa snapshot from
// cfg. Called after a LIMIT change so the running PI loop sees the new
// setpoint immediately (otherwise the change only applies on next start).
static void refreshActiveLimit()
{
    if (activeMotor >= 0 && motors[activeMotor].state == STATE_RUNNING)
    {
        int ai = actionIdx(motors[activeMotor].action);
        motors[activeMotor].activeLimitMa = cfg[activeMotor].limitMa[ai];
    }
}

static const char *motorTag(int mIdx) { return (mIdx == 0) ? "M1" : (mIdx == 1) ? "M2" : "All"; }

// ═══════════════════════════════════════════════════════════════════
// Command dispatch
// ═══════════════════════════════════════════════════════════════════
// processCommand walks through commands roughly in order of frequency.
// Most commands return early after handling. Anything unrecognised falls
// through to printHelp() at the bottom.
void processCommand(const String &raw)
{
    String cmd = raw; cmd.trim();
    String upper = cmd; upper.toUpperCase();

    // Direct motor actions and emergency stop ─ exact-match for speed.
    if      (upper == "M1 TIGHTEN") { startMotor(0, ACTION_TIGHTEN); return; }
    else if (upper == "M1 LOOSEN")  { startMotor(0, ACTION_LOOSEN);  return; }
    else if (upper == "M2 TIGHTEN") { startMotor(1, ACTION_TIGHTEN); return; }
    else if (upper == "M2 LOOSEN")  { startMotor(1, ACTION_LOOSEN);  return; }
    else if (upper == "STOP")       { stopAllMotors(); return; }
    else if (upper == "STATUS")     { printStatus(); return; }

    // Toggle the 200 ms periodic status line emitted from main loop.
    if (upper == "LOG")
    {
        logEnabled = !logEnabled;
        Serial.printf("Periodic logging: %s\n", logEnabled ? "ON (200ms)" : "OFF");
        return;
    }
    // Toggle a per-control-cycle CSV stream from inside controlLoop().
    // Useful for PI tuning; emits a header line on START so a script can
    // detect the schema.
    if (upper == "FAST")
    {
        fastLogEnabled = !fastLogEnabled;
        if (fastLogEnabled)
            Serial.println("FAST_START,time_ms,motor,current_mA,pwm,rpm,integral");
        else
            Serial.println("FAST_STOP");
        return;
    }

    // ── LIMIT [M1|M2] [T|L] <mA> ────────────────────────────────────
    if (upper.startsWith("LIMIT "))
    {
        String arg = upper.substring(6);
        int mIdx = parseMotorPrefix(arg);
        int aIdx = parseActionPrefix(arg);
        float v = arg.toFloat();
        if (v < 50 || v > 1500) { Serial.println("[ERR] LIMIT must be 50-1500 mA"); return; }

        forMotors(mIdx, [&](int i) {
            int as = (aIdx >= 0) ? aIdx : 0;
            int ae = (aIdx >= 0) ? aIdx : 1;
            for (int a = as; a <= ae; a++) cfg[i].limitMa[a] = v;
        });
        refreshActiveLimit();
        const char *aStr = (aIdx == 0) ? " tighten" : (aIdx == 1) ? " loosen" : "";
        Serial.printf("[CFG] %s%s limit set to %.0f mA\n", motorTag(mIdx), aStr, v);
        return;
    }

    // ── KP [M1|M2] <val> ────────────────────────────────────────────
    if (upper.startsWith("KP "))
    {
        String arg = upper.substring(3);
        int mIdx = parseMotorPrefix(arg);
        float v = arg.toFloat();
        if (v < 0 || v > 100) { Serial.println("[ERR] KP must be 0-100"); return; }
        forMotors(mIdx, [&](int i) { cfg[i].kp = v; });
        Serial.printf("[CFG] %s Kp = %.3f\n", motorTag(mIdx), v);
        return;
    }

    // ── KI [M1|M2] <val> ────────────────────────────────────────────
    if (upper.startsWith("KI "))
    {
        String arg = upper.substring(3);
        int mIdx = parseMotorPrefix(arg);
        float v = arg.toFloat();
        if (v < 0 || v > 100) { Serial.println("[ERR] KI must be 0-100"); return; }
        forMotors(mIdx, [&](int i) { cfg[i].ki = v; });
        Serial.printf("[CFG] %s Ki = %.4f\n", motorTag(mIdx), v);
        return;
    }

    // ── SLEW [M1|M2] T|L <val> ──────────────────────────────────────
    if (upper.startsWith("SLEW "))
    {
        String arg = upper.substring(5);
        int mIdx = parseMotorPrefix(arg);
        int aIdx = parseActionPrefix(arg);
        if (aIdx < 0) { Serial.println("[ERR] SLEW requires T or L"); return; }
        int v = arg.toInt();
        if (v <= 0) { Serial.println("[ERR] SLEW value must be > 0"); return; }
        forMotors(mIdx, [&](int i) { cfg[i].slew[aIdx] = v; });
        Serial.printf("[CFG] %s slew %s = %d\n",
                      motorTag(mIdx), aIdx == 0 ? "tighten" : "loosen", v);
        return;
    }

    // ── RAMP [M1|M2] <ms> ───────────────────────────────────────────
    if (upper.startsWith("RAMP "))
    {
        String arg = upper.substring(5);
        int mIdx = parseMotorPrefix(arg);
        int v = arg.toInt();
        if (v < 0 || v > 5000) { Serial.println("[ERR] RAMP must be 0-5000 ms"); return; }
        forMotors(mIdx, [&](int i) { cfg[i].rampMs = (unsigned long)v; });
        Serial.printf("[CFG] %s ramp = %d ms\n", motorTag(mIdx), v);
        return;
    }

    // ── PWMMIN [M1|M2] <val> ────────────────────────────────────────
    if (upper.startsWith("PWMMIN "))
    {
        String arg = upper.substring(7);
        int mIdx = parseMotorPrefix(arg);
        int v = arg.toInt();
        if (v < 0 || v > 255) { Serial.println("[ERR] PWMMIN must be 0-255"); return; }
        forMotors(mIdx, [&](int i) { cfg[i].pwmMin = v; });
        Serial.printf("[CFG] %s pwmMin = %d\n", motorTag(mIdx), v);
        return;
    }

    // ── PWMMAX [M1|M2] T|L <val> ────────────────────────────────────
    if (upper.startsWith("PWMMAX "))
    {
        String arg = upper.substring(7);
        int mIdx = parseMotorPrefix(arg);
        int aIdx = parseActionPrefix(arg);
        if (aIdx < 0) { Serial.println("[ERR] PWMMAX requires T or L"); return; }
        int v = arg.toInt();
        if (v < 0 || v > 255) { Serial.println("[ERR] PWMMAX must be 0-255"); return; }
        forMotors(mIdx, [&](int i) { cfg[i].pwmCeiling[aIdx] = v; });
        Serial.printf("[CFG] %s pwmMax %s = %d\n",
                      motorTag(mIdx), aIdx == 0 ? "tighten" : "loosen", v);
        return;
    }

    printHelp();
}

// ═══════════════════════════════════════════════════════════════════
// Status / logging
// ═══════════════════════════════════════════════════════════════════
void printStatus()
{
    float currentMa = readCurrentMa();
    Serial.println("════════════════════════════════════════");
    for (int i = 0; i < NUM_MOTORS; i++)
    {
        const MotorConfig &c = cfg[i];
        Serial.printf("  M%d: limit T=%.0f L=%.0f mA  Kp=%.3f Ki=%.4f  ramp=%lums\n",
                      i + 1, c.limitMa[0], c.limitMa[1], c.kp, c.ki, c.rampMs);
        Serial.printf("       pwm[min=%d max=%d]  ceiling T=%d L=%d  slew T=%d L=%d  gear=%.1f\n",
                      c.pwmMin, c.pwmMax,
                      c.pwmCeiling[0], c.pwmCeiling[1],
                      c.slew[0], c.slew[1], c.gearRatio);
    }
    Serial.printf("  Measured current: %.1f mA\n", currentMa);
    Serial.printf("  Active motor: %s\n",
                  activeMotor >= 0
                      ? String("M" + String(activeMotor + 1)).c_str()
                      : "none");
    for (int i = 0; i < NUM_MOTORS; i++)
    {
        Motor &m = motors[i];
        updateMotorRpm(m);
        float shaftRpm = (cfg[i].gearRatio > 0) ? (m.motorRpm / cfg[i].gearRatio) : 0.0f;
        Serial.printf("  M%d: %-8s %-8s  PWM=%3d (%2d%%)  RPM=%.0f (shaft %.1f)  limit=%.0f mA\n",
                      i + 1, stateStr(m.state), actionStr(m.action),
                      m.pwmValue, (int)(m.pwmValue * 100 / 255),
                      m.motorRpm, shaftRpm, m.activeLimitMa);
    }
    Serial.printf("  Buttons: %d %d %d %d\n",
                  buttonPressed(0), buttonPressed(1),
                  buttonPressed(2), buttonPressed(3));
    Serial.println("════════════════════════════════════════");
}

void printLogLine()
{
    float currentMa = readCurrentMa();
    for (int i = 0; i < NUM_MOTORS; i++)
        updateMotorRpm(motors[i]);

    // CSV: timestamp,active,m1_state,m1_action,m1_pwm,m1_rpm,m2_state,m2_action,m2_pwm,m2_rpm,current_mA
    Serial.printf("LOG,%lu,%d,%s,%s,%d,%.0f,%s,%s,%d,%.0f,%.1f\n",
                  millis(), activeMotor + 1,
                  stateStr(motors[0].state), actionStr(motors[0].action),
                  motors[0].pwmValue, motors[0].motorRpm,
                  stateStr(motors[1].state), actionStr(motors[1].action),
                  motors[1].pwmValue, motors[1].motorRpm,
                  currentMa);
}

void printHelp()
{
    Serial.println("Commands:");
    Serial.println("  M1 TIGHTEN | M1 LOOSEN | M2 TIGHTEN | M2 LOOSEN");
    Serial.println("  STOP | STATUS | LOG | FAST");
    Serial.println("  LIMIT [M1|M2] [T|L] <mA>");
    Serial.println("  KP    [M1|M2] <val>     KI [M1|M2] <val>");
    Serial.println("  SLEW  [M1|M2] T|L <val>");
    Serial.println("  RAMP  [M1|M2] <ms>");
    Serial.println("  PWMMIN [M1|M2] <val>    PWMMAX [M1|M2] T|L <val>");
}
