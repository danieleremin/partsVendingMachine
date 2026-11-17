// config.h -- pins, tuning constants and the bin table (firmware spec, sections
// 2.1 and 4). Everything that may need adjusting on the real machine is here.

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------

// ULN2003 driver inputs IN1..IN4.
constexpr uint8_t PIN_STEP_IN1 = 8;
constexpr uint8_t PIN_STEP_IN2 = 9;
constexpr uint8_t PIN_STEP_IN3 = 10;
constexpr uint8_t PIN_STEP_IN4 = 11;

// Gate servo signal.
constexpr uint8_t PIN_SERVO = 6;

// Hall sensor (read with INPUT_PULLUP) and the level it outputs while the
// magnet is in front of it. Confirm the level on the hardware.
constexpr uint8_t PIN_HALL = 3;
constexpr auto HALL_ACTIVE_LEVEL = LOW;

// Buttons, wired to GND (INPUT_PULLUP, LOW = pressed).
constexpr uint8_t PIN_BTN_NEXT = 4;
constexpr uint8_t PIN_BTN_DISPENSE = 5;

// ---------------------------------------------------------------------------
// Carousel
// ---------------------------------------------------------------------------

// Stepper steps per carousel revolution. Verified: holds position over many
// revolutions.
constexpr int32_t STEPS_PER_REV = 2048;

// Speed of normal moves. Verified.
constexpr int STEPPER_RPM = 15;

// Slower speed while searching for the magnet, for a repeatable home position.
constexpr int HOMING_RPM = 5;

constexpr uint8_t BIN_COUNT = 10;

// Steps from the magnet trigger point to bin 0 aligned over the dispense hole,
// 0 .. STEPS_PER_REV-1. Calibrate on the built machine.
constexpr int32_t HOME_OFFSET_STEPS = 0;
static_assert(HOME_OFFSET_STEPS >= 0 && HOME_OFFSET_STEPS < STEPS_PER_REV,
              "HOME_OFFSET_STEPS must be less than one revolution");

// Homing gives up after this many steps (1.2 revolutions).
constexpr int32_t HOMING_TIMEOUT_STEPS = STEPS_PER_REV * 12 / 10;

// A drift correction larger than this prints a warning (slipping or a loose
// magnet).
constexpr int32_t DRIFT_WARN_STEPS = 20;

// The coils stay powered this long after the last step of a move, so the rotor
// finishes that step before the power is cut.
constexpr uint16_t COIL_HOLD_MS = 20;

// ---------------------------------------------------------------------------
// Gate
// ---------------------------------------------------------------------------

// Servo angles. Tune both on the real gate.
constexpr int GATE_CLOSED_DEG = 90;
constexpr int GATE_OPEN_DEG = 155;

// Time allowed for the servo to finish a move.
constexpr uint16_t SERVO_TRAVEL_MS = 300;

// Pause after a carousel move before the gate may open.
constexpr uint16_t SETTLE_MS = 200;

// Starting gate open time for uncalibrated bins.
constexpr uint16_t DEFAULT_OPEN_MS = 150;

// ---------------------------------------------------------------------------
// Buttons and serial
// ---------------------------------------------------------------------------

// A button state counts once the raw reading has been stable this long.
// Verified in the button bench test.
constexpr uint16_t DEBOUNCE_MS = 50;

constexpr uint32_t SERIAL_BAUD = 115200;

// The `t` command: pause between test cycles, and the largest count it accepts.
// A test run cannot be interrupted, so the count is capped.
constexpr uint16_t TEST_INTERVAL_MS = 1500;
constexpr uint16_t TEST_MAX_COUNT = 100;

// How often the error message repeats while in the error state.
constexpr uint16_t ERROR_REPRINT_MS = 5000;

// ---------------------------------------------------------------------------
// Bin table
// ---------------------------------------------------------------------------

// One entry per bin. Names are placeholders until bin contents are decided.
// Calibration (spec section 7) only changes openMs: the `p` serial command
// prints this table with the current values, ready to paste back in here.
struct Bin {
  const char* name;
  uint16_t    openMs;   // how long the gate stays open
};

// `inline` lets every source file include this header and share one table.
inline Bin bins[BIN_COUNT] = {
  {"Bin 0", DEFAULT_OPEN_MS}, {"Bin 1", DEFAULT_OPEN_MS}, {"Bin 2", DEFAULT_OPEN_MS},
  {"Bin 3", DEFAULT_OPEN_MS}, {"Bin 4", DEFAULT_OPEN_MS}, {"Bin 5", DEFAULT_OPEN_MS},
  {"Bin 6", DEFAULT_OPEN_MS}, {"Bin 7", DEFAULT_OPEN_MS}, {"Bin 8", DEFAULT_OPEN_MS},
  {"Bin 9", DEFAULT_OPEN_MS},
};
