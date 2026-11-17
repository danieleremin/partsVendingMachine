// gate.h -- the servo-driven gate over the dispense hole.

#pragma once

#include <Arduino.h>

// Closes the gate. Call once at startup, before the carousel moves.
void gateBegin();

// Runs one dispense cycle: opens the gate, waits openMs, closes it again.
// Blocks until the gate is closed and the servo is detached.
void gateCycle(uint16_t openMs);

// Gate safety flag: true only when the gate is closed and SERVO_TRAVEL_MS has
// passed since the close command. The carousel must not move otherwise.
bool gateIsClosed();
