#include "gate.h"

#include <Servo.h>

#include "config.h"

static Servo servo;
static bool closed = false;   // see gateIsClosed()

static void attachServo() {
  servo.attach(PIN_SERVO);
  // On the Uno R4, attach() sets the signal pin HIGH, and the library only
  // brings it LOW again at the end of its first timed pulse, up to 40 ms
  // later. The servo would read that as one over-long pulse and kick towards
  // its end stop. Pull the pin back LOW; the library then sends normal pulses.
  digitalWrite(PIN_SERVO, LOW);
}

static void detachServo() {
  // detach() stops the pulse timer wherever it happens to be. Let a pulse that
  // is in progress finish first (2.4 ms at most), so the servo's last pulse is
  // a complete one and the signal pin is not left HIGH.
  const uint32_t startUs = micros();
  while (digitalRead(PIN_SERVO) == HIGH && micros() - startUs < 3000) {
  }
  servo.detach();
  digitalWrite(PIN_SERVO, LOW);
}

// Closes the gate from an attached servo and detaches it again.
static void closeAndDetach() {
  servo.write(GATE_CLOSED_DEG);
  delay(SERVO_TRAVEL_MS);
  detachServo();
  closed = true;
}

void gateBegin() {
  attachServo();
  closeAndDetach();
}

void gateCycle(uint16_t openMs) {
  closed = false;
  attachServo();
  servo.write(GATE_OPEN_DEG);
  delay(openMs);   // open time runs from the open command to the close command
  closeAndDetach();
}

bool gateIsClosed() {
  return closed;
}
