// carousel.h -- the rotating carousel: homing, position tracking and moves.
//
// The carousel only ever turns forward, one step at a time, and its coils are
// off whenever it is not moving. It refuses to move unless gateIsClosed().

#pragma once

#include <Arduino.h>

enum class HomeResult : uint8_t {
  Ok,
  MagnetNotFound,   // the hall sensor never became active
  SensorStuck,      // the hall sensor never became inactive
  GateNotClosed,    // refused by the gate safety rule
};

// Configures the stepper and hall sensor pins, coils off.
void carouselBegin();

// Finds the magnet and leaves bin 0 over the dispense hole. Blocks.
HomeResult carouselHome();

// Turns forward to the given bin, correcting drift if the magnet passes the
// sensor on the way. Blocks. Returns false if the gate safety rule refused the
// move. Only valid after a successful carouselHome().
bool carouselMoveToBin(uint8_t bin);

// True if the carousel is at the given bin's position.
bool carouselAtBin(uint8_t bin);

// Blocks until SETTLE_MS has passed since the carousel last stopped.
void carouselWaitSettled();

// Position in steps past bin 0, 0 .. STEPS_PER_REV-1.
int32_t carouselPos();

// Size of the most recent drift correction in steps, 0 if none since homing.
// Negative means the carousel was behind where the step count said it was.
int32_t carouselLastCorrection();
