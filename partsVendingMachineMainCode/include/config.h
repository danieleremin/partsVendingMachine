/* config.h -- single source of truth for all machine constants.
Included directly by the firmware AND parsed at runtime by the Python
simulation (pythonSimulation/config_loader.py). To keep that parser simple,
this file follows strict rules:
  - Only `#define NAME VALUE` lines (plus `#pragma once` and comments).
  - VALUE is a plain integer or decimal literal: no expressions, no suffixes
    (e.g. no 3200UL), no references to other names.
  - One define per line. Each define's comment sits on its own line(s)
    directly above it, starting with `//`.
  - Derived values (bin angle, steps per bin, valid bin range) are NOT stored
    here. The firmware and the Python loader each compute them from the raw
    values below, so there is never a second number that can drift.
Values marked PROVISIONAL are placeholders until real hardware is measured. */

#pragma once

// ---------------------------------------------------------------------------
// Carousel geometry
// ---------------------------------------------------------------------------

// Number of bins on the carousel. Bins are numbered 0 .. BIN_COUNT-1, and the
// command protocol's valid bin range follows this value automatically.
// Derived (not stored): bin angle = 360 / BIN_COUNT degrees.
// If you change this, re-check that STEPS_PER_REV divides evenly by it.
#define BIN_COUNT 12

// PROVISIONAL: stepper steps per full carousel revolution, including
// microstepping and any gear/belt ratio. Placeholder assumes a 200-step motor
// at 1/16 microstepping, direct drive. Replace once the motor, driver
// microstep setting and drive ratio are known.
// Derived (not stored): steps per bin = STEPS_PER_REV / BIN_COUNT.
// WARNING: with the placeholder values this is NOT a whole number
// (3200 / 12 = 266.67 steps per bin). Until real hardware fixes that, bin
// positions must be computed as round(bin * STEPS_PER_REV / BIN_COUNT) from
// home, never by adding up a rounded per-bin step count (the rounding error
// would accumulate and the carousel would drift off-bin).
// When choosing the real motor, microstepping and gearing, prefer a total that
// divides evenly by BIN_COUNT, e.g. 2400 (1/4 microstep, 3:1 gear) or 4800
// (1/8 microstep, 3:1 gear) for 12 bins.
#define STEPS_PER_REV 3200

// ---------------------------------------------------------------------------
// Gate / servo motion
// ---------------------------------------------------------------------------

// PROVISIONAL: servo angle (degrees) at which the gate fully blocks the chute.
// Replace with the angle measured on the assembled gate.
#define GATE_CLOSED_ANGLE 0

// PROVISIONAL: servo angle (degrees) at which the gate is fully open.
// Replace with the angle measured on the assembled gate.
#define GATE_OPEN_ANGLE 90

// PROVISIONAL: time (ms) allowed for the servo to travel between closed and
// open. Replace with measured travel time plus a safety margin.
#define GATE_TRAVEL_MS 500

// ---------------------------------------------------------------------------
// Dispense behavior
// ---------------------------------------------------------------------------

// PROVISIONAL: time (ms) the gate stays open, watching the IR beam for a
// falling part, before the attempt is judged. Replace with the measured
// worst-case drop time from bin to beam plus margin.
#define SENSOR_WAIT_MS 300

// Total gate openings allowed per dispense request, including the first one.
// Attempts after the first are retries (no re-rotation). Must be >= 1.
#define MAX_ATTEMPTS 3

// ---------------------------------------------------------------------------
// Command protocol (see docs/protocol.md)
// ---------------------------------------------------------------------------

// Serial baud rate for the text command protocol.
#define SERIAL_BAUD 115200

// Maximum command line length in characters, excluding the line terminator.
// Longer lines are discarded and answered with ERR LINE_TOO_LONG.
#define CMD_MAX_LINE_LEN 32

// ---------------------------------------------------------------------------
// Pin assignments (PLACEHOLDERS for Arduino UNO R4 WiFi; confirm when wiring)
// ---------------------------------------------------------------------------

// Stepper driver STEP input (one pulse = one (micro)step).
#define PIN_STEPPER_STEP 2

// Stepper driver DIR input (sets rotation direction).
#define PIN_STEPPER_DIR 3

// Stepper driver ENABLE input (active LOW on A4988/DRV8825-style drivers).
#define PIN_STEPPER_ENABLE 4

// IR break-beam emitter power pin (lets firmware switch the emitter off to
// check for ambient-light false readings).
#define PIN_IR_EMITTER 5

// IR break-beam receiver digital input.
#define PIN_IR_RECEIVER 6

// Optional: manual test-trigger button (wired to GND, use INPUT_PULLUP).
#define PIN_TEST_BUTTON 7

// Gate servo signal (must be a PWM-capable pin).
#define PIN_SERVO 9

// Optional: status LED (13 = on-board LED_BUILTIN).
#define PIN_STATUS_LED 13
