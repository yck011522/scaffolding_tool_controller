// serial_cmd.h — Serial command parser, status output, periodic logging.
//
// Commands (case-insensitive):
//   M1 TIGHTEN | M1 LOOSEN | M2 TIGHTEN | M2 LOOSEN
//   STOP | STATUS | LOG | FAST
//   LIMIT [M1|M2] [T|L] <mA>          omit motor/action ⇒ apply to both
//   KP    [M1|M2] <val>
//   KI    [M1|M2] <val>
//   SLEW  [M1|M2] T|L <val>
//   RAMP  [M1|M2] <ms>
//   PWMMIN [M1|M2] <val>              dead-band floor
//   PWMMAX [M1|M2] T|L <val>          per-action ceiling

#pragma once

#include <Arduino.h>

extern bool logEnabled;

void processCommand(const String &raw);
void printStatus();
void printLogLine();
void printHelp();
