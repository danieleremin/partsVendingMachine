#include "carousel.h"

#include <Stepper.h>

#include "config.h"
#include "gate.h"

static const uint8_t STEP_PINS[] = {PIN_STEP_IN1, PIN_STEP_IN2, PIN_STEP_IN3, PIN_STEP_IN4};

// The Stepper library needs the pins in the order IN1, IN3, IN2, IN4. Any
// other order makes the 28BYJ-48 buzz instead of turning.
static Stepper stepper(STEPS_PER_REV, PIN_STEP_IN1, PIN_STEP_IN3, PIN_STEP_IN2, PIN_STEP_IN4);

// Where the magnet's leading edge sits, in the same units as pos: the trigger
// point is HOME_OFFSET_STEPS before bin 0.
static constexpr int32_t HOME_REFERENCE_POS = (STEPS_PER_REV - HOME_OFFSET_STEPS) % STEPS_PER_REV;

static int32_t pos = 0;   // steps past bin 0; meaningful only once homed
static int32_t lastCorrection = 0;
static uint32_t lastStopMs = 0;

static bool hallActive() {
  return digitalRead(PIN_HALL) == HALL_ACTIVE_LEVEL;
}

// Ends a move: lets the rotor finish its last step, then turns the coils off.
// The gearbox holds the carousel in place.
static void stopMotor() {
  delay(COIL_HOLD_MS);
  for (uint8_t pin : STEP_PINS) {
    digitalWrite(pin, LOW);
  }
  lastStopMs = millis();
}

// round(bin * STEPS_PER_REV / BIN_COUNT) in integer math. Computed from bin 0
// every time, so the fractional steps per bin never add up.
static int32_t binTarget(uint8_t bin) {
  return (2 * (int32_t)bin * STEPS_PER_REV + BIN_COUNT) / (2 * BIN_COUNT);
}

static int32_t stepsForwardTo(int32_t target) {
  return (target - pos + STEPS_PER_REV) % STEPS_PER_REV;
}

// Steps forward until the hall sensor reads `wanted`, taking the steps out of
// `budget`. Returns false if the budget runs out first.
static bool stepUntilHall(bool wanted, int32_t& budget) {
  while (hallActive() != wanted) {
    if (budget <= 0) {
      return false;
    }
    stepper.step(1);
    budget--;
  }
  return true;
}

void carouselBegin() {
  for (uint8_t pin : STEP_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  pinMode(PIN_HALL, INPUT_PULLUP);
}

HomeResult carouselHome() {
  if (!gateIsClosed()) {
    return HomeResult::GateNotClosed;
  }

  stepper.setSpeed(HOMING_RPM);
  int32_t budget = HOMING_TIMEOUT_STEPS;
  HomeResult result = HomeResult::Ok;

  // If the magnet is already at the sensor, move off it first, so the trigger
  // point is always approached from the same side. Then find its leading edge.
  if (!stepUntilHall(false, budget)) {
    result = HomeResult::SensorStuck;
  } else if (!stepUntilHall(true, budget)) {
    result = HomeResult::MagnetNotFound;
  } else {
    for (int32_t i = 0; i < HOME_OFFSET_STEPS; i++) {
      stepper.step(1);
    }
    pos = 0;
    lastCorrection = 0;
  }

  stopMotor();
  return result;
}

bool carouselMoveToBin(uint8_t bin) {
  if (!gateIsClosed()) {
    return false;
  }

  const int32_t target = binTarget(bin);
  int32_t remaining = stepsForwardTo(target);
  if (remaining == 0) {
    return true;
  }

  stepper.setSpeed(STEPPER_RPM);
  bool wasActive = hallActive();
  bool corrected = false;
  while (remaining > 0) {
    stepper.step(1);
    pos = (pos + 1) % STEPS_PER_REV;
    remaining--;

    // Drift correction: the sensor going active is the same reference point
    // homing uses. A move is shorter than one revolution, so the magnet can
    // only arrive once; any later edge in the same move is ignored as noise.
    const bool active = hallActive();
    if (active && !wasActive && !corrected) {
      int32_t correction = HOME_REFERENCE_POS - pos;
      if (correction > STEPS_PER_REV / 2) {
        correction -= STEPS_PER_REV;
      } else if (correction < -STEPS_PER_REV / 2) {
        correction += STEPS_PER_REV;
      }
      pos = HOME_REFERENCE_POS;
      remaining = stepsForwardTo(target);
      lastCorrection = correction;
      corrected = true;
    }
    wasActive = active;
  }
  stopMotor();

  if (corrected && abs(lastCorrection) > DRIFT_WARN_STEPS) {
    Serial.print(F("WARNING: drift correction of "));
    Serial.print(lastCorrection);
    Serial.println(F(" steps (slipping or a loose magnet?)"));
  }
  return true;
}

bool carouselAtBin(uint8_t bin) {
  return pos == binTarget(bin);
}

void carouselWaitSettled() {
  // millis() drops fractions of a millisecond, so wait one more than the
  // difference to make the pause at least SETTLE_MS.
  const uint32_t sinceStopMs = millis() - lastStopMs;
  if (sinceStopMs <= SETTLE_MS) {
    delay(SETTLE_MS + 1 - sinceStopMs);
  }
}

int32_t carouselPos() {
  return pos;
}

int32_t carouselLastCorrection() {
  return lastCorrection;
}
