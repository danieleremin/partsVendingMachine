// buttons.h -- debounced push buttons with press detection.

#pragma once

#include <Arduino.h>

// A push button wired between a pin and GND (INPUT_PULLUP, LOW = pressed).
class Button {
 public:
  explicit Button(uint8_t pin) : pin_(pin) {}

  void begin();

  // Call on every pass of loop(). Returns true once per press: when the
  // debounced state goes from released to pressed. Holding does not repeat.
  bool pressed();

  // Call after anything that blocked. Takes the button's current level as its
  // state without reporting a press, so a press made in the meantime is
  // ignored rather than acted on late.
  void resync();

 private:
  uint8_t pin_;
  bool stable_ = false;    // debounced state, true = pressed
  bool lastRaw_ = false;   // latest raw reading
  uint32_t rawSinceMs_ = 0;   // when the raw reading last changed
};
