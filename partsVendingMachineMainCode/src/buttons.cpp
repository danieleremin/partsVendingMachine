#include "buttons.h"

#include "config.h"

void Button::begin() {
  pinMode(pin_, INPUT_PULLUP);
  resync();
}

bool Button::pressed() {
  const bool raw = digitalRead(pin_) == LOW;
  const uint32_t now = millis();
  if (raw != lastRaw_) {
    lastRaw_ = raw;
    rawSinceMs_ = now;
  }
  if (raw == stable_ || now - rawSinceMs_ < DEBOUNCE_MS) {
    return false;
  }
  stable_ = raw;
  return stable_;   // report the press, not the release
}

void Button::resync() {
  stable_ = lastRaw_ = (digitalRead(pin_) == LOW);
  rawSinceMs_ = millis();
}
