#include <Arduino.h>
#include "config.h"

enum states {
  IDLE,
  ROTATING,
  GATE_OPENING,
  WAITING_FOR_SENSOR,
  GATE_CLOSING,
  SUCCESS,
  RETRY,
  FAIL
};

states currentState = IDLE;

// put function declarations here
void setup() {
  // put your setup code here, to run once
}

void loop() {
  // put your main code here, to run repeatedly
  switch (currentState) {
    case IDLE:
      // Handle idle state
      break;
    case ROTATING:
      // Handle rotating state
      break;
    case GATE_OPENING:
      // Handle gate opening state
      break;
    case WAITING_FOR_SENSOR:
      // Handle waiting for sensor state
      break;
    case GATE_CLOSING:
      // Handle gate closing state
      break;
    case SUCCESS:
      // Handle success state
      break;
    case RETRY:
      // Handle retry state
      break;
    case FAIL:
      // Handle fail state
      break;
  }
}

// put function definitions here
