// Parts vending machine firmware, v1. See vending-machine-firmware-spec.md.
//
// main.cpp -- startup, the idle/error state, the two buttons and the serial
// commands. Moves and dispense cycles block; whatever arrives while one is
// running is thrown away afterwards (the busy lock), never acted on late.

#include <Arduino.h>

#include "buttons.h"
#include "carousel.h"
#include "config.h"
#include "gate.h"

static const char FW_VERSION[] = "1.0.0";

enum class State : uint8_t { Idle, Error };

static State state = State::Error;   // nothing may move until homing succeeds
static const __FlashStringHelper* errorMessage = nullptr;
static uint32_t errorPrintedMs = 0;
static uint8_t selected = 0;   // the selected bin

static Button btnNext(PIN_BTN_NEXT);
static Button btnDispense(PIN_BTN_DISPENSE);

// Serial line reader.
static char line[32];           // far longer than any valid command
static uint8_t lineLen = 0;     // sizeof(line) marks a line that was too long
static bool skipLine = false;   // the rest of the current line is to be dropped

// Machine actions

static void printSelectedBin() {
  Serial.print(F("Bin "));
  Serial.print(selected);
  Serial.print(F(": "));
  Serial.println(bins[selected].name);
}

static void enterErrorState(const __FlashStringHelper* message) {
  state = State::Error;
  errorMessage = message;
  errorPrintedMs = millis();
  Serial.println(message);
}

// Call after every action that blocked. Presses and serial input from the busy
// period are discarded, and so is the remainder of a line that was only partly
// received, so that a fragment of a command is never run.
static void endBusy() {
  btnNext.resync();
  btnDispense.resync();

  bool dropped = false;
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    const bool endOfLine = (c == '\n' || c == '\r');
    if (!endOfLine || lineLen > 0) {
      dropped = true;
    }
    skipLine = !endOfLine;
    if (endOfLine) {
      lineLen = 0;
    }
  }
  if (dropped) {
    Serial.println(F("(input received while busy was ignored)"));
  }
}

static bool requireIdle() {
  if (state == State::Idle) {
    return true;
  }
  Serial.println(F("Not available in the error state. Send H to retry homing."));
  return false;
}

static bool moveToSelected() {
  if (carouselMoveToBin(selected)) {
    return true;
  }
  Serial.println(F("ERROR: carousel move refused (gate not closed)"));
  return false;
}

static void selectAndGo(uint8_t bin) {
  selected = bin;
  if (moveToSelected()) {
    printSelectedBin();
  }
}

// One dispense cycle on the selected bin.
static void dispenseSelected() {
  if (!carouselAtBin(selected) && !moveToSelected()) {
    return;
  }
  carouselWaitSettled();

  const Bin& bin = bins[selected];
  gateCycle(bin.openMs);

  Serial.print(F("Dispensed from bin "));
  Serial.print(selected);
  Serial.print(F(" ("));
  Serial.print(bin.name);
  Serial.print(F("), open "));
  Serial.print(bin.openMs);
  Serial.println(F(" ms"));
}

static void home() {
  Serial.println(F("Homing..."));
  switch (carouselHome()) {
    case HomeResult::Ok:
      state = State::Idle;
      selected = 0;
      printSelectedBin();
      break;
    case HomeResult::MagnetNotFound:
      enterErrorState(F("ERROR: homing failed (magnet not found)"));
      break;
    case HomeResult::SensorStuck:
      enterErrorState(F("ERROR: homing failed (hall sensor stuck active)"));
      break;
    case HomeResult::GateNotClosed:
      enterErrorState(F("ERROR: homing refused (gate not closed)"));
      break;
  }
}

static void pressNext() {
  selectAndGo((selected + 1) % BIN_COUNT);
  endBusy();
}

static void pressDispense() {
  dispenseSelected();
  endBusy();
}

// Serial commands

static const char* skipSpaces(const char* p) {
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  return p;
}

static bool atEnd(const char* p) {
  return *skipSpaces(p) == '\0';
}

// Reads an unsigned decimal number at p, after optional spaces, and moves p
// past it. Returns false if there is no number there or it is above `max`.
static bool parseNumber(const char*& p, uint32_t max, uint32_t& value) {
  p = skipSpaces(p);
  if (!isDigit(*p)) {
    return false;
  }
  uint32_t v = 0;
  for (; isDigit(*p); p++) {
    v = v * 10 + (uint32_t)(*p - '0');
    if (v > max) {
      return false;
    }
  }
  value = v;
  return true;
}

static void printBadArguments() {
  Serial.println(F("Bad arguments. Type h for help."));
}

static void printHelp() {
  Serial.print(F(
      "Commands:\r\n"
      "  h             list commands\r\n"
      "  s             status and the bin table\r\n"
      "  n             next bin (same as the Next button)\r\n"
      "  d             dispense (same as the Dispense button)\r\n"
      "  g<n>          go to bin n and select it, e.g. g3\r\n"
      "  H             re-run homing\r\n"
      "  o<n> <ms>     set bin n's open time, RAM only, e.g. o3 180\r\n"
      "  t<n> <count>  select bin n and test-dispense it count times, e.g. t3 20\r\n"
      "  p             print the bin table for config.h\r\n"
      "Bins are 0-"));
  Serial.print(BIN_COUNT - 1);
  Serial.print(F(", open times 0-"));
  Serial.print(UINT16_MAX);
  Serial.print(F(" ms, test counts 1-"));
  Serial.print(TEST_MAX_COUNT);
  Serial.println('.');
}

static void printStatus() {
  Serial.print(F("State: "));
  Serial.println(state == State::Idle ? F("IDLE") : F("ERROR"));

  Serial.print(F("Position: "));
  if (state == State::Idle) {
    Serial.print(carouselPos());
    Serial.print(F(" of "));
    Serial.print(STEPS_PER_REV);
    Serial.println(F(" steps"));
  } else {
    Serial.println(F("unknown (not homed)"));
  }

  Serial.print(F("Selected bin: "));
  Serial.println(selected);
  Serial.print(F("Last drift correction: "));
  Serial.print(carouselLastCorrection());
  Serial.println(F(" steps"));

  Serial.println(F("Bins:"));
  for (uint8_t i = 0; i < BIN_COUNT; i++) {
    Serial.print(i == selected ? F("  > ") : F("    "));
    Serial.print(i);
    Serial.print(F(": "));
    Serial.print(bins[i].name);
    Serial.print(F(", open "));
    Serial.print(bins[i].openMs);
    Serial.println(F(" ms"));
  }
}

// Prints the bin table as it is written in config.h, five entries to a line.
static void printBinTable() {
  Serial.println(F("inline Bin bins[BIN_COUNT] = {"));
  for (uint8_t i = 0; i < BIN_COUNT; i++) {
    Serial.print(i % 5 == 0 ? F("  {\"") : F(" {\""));
    Serial.print(bins[i].name);
    Serial.print(F("\", "));
    Serial.print(bins[i].openMs);
    Serial.print(F("},"));
    if (i % 5 == 4 || i == BIN_COUNT - 1) {
      Serial.println();
    }
  }
  Serial.println(F("};"));
}

static void commandGo(const char* args) {
  uint32_t bin;
  if (!parseNumber(args, BIN_COUNT - 1, bin) || !atEnd(args)) {
    printBadArguments();
    return;
  }
  if (!requireIdle()) {
    return;
  }
  selectAndGo((uint8_t)bin);
  endBusy();
}

static void commandSetOpenTime(const char* args) {
  uint32_t bin;
  uint32_t ms;
  if (!parseNumber(args, BIN_COUNT - 1, bin) || !parseNumber(args, UINT16_MAX, ms) || !atEnd(args)) {
    printBadArguments();
    return;
  }
  bins[bin].openMs = (uint16_t)ms;
  Serial.print(F("Bin "));
  Serial.print(bin);
  Serial.print(F(" ("));
  Serial.print(bins[bin].name);
  Serial.print(F(") open time set to "));
  Serial.print(ms);
  Serial.println(F(" ms (RAM only)"));
}

static void commandTest(const char* args) {
  uint32_t bin;
  uint32_t count;
  if (!parseNumber(args, BIN_COUNT - 1, bin) || !parseNumber(args, TEST_MAX_COUNT, count) ||
      count == 0 || !atEnd(args)) {
    printBadArguments();
    return;
  }
  if (!requireIdle()) {
    return;
  }

  selected = (uint8_t)bin;
  if (moveToSelected()) {
    for (uint32_t i = 1; i <= count; i++) {
      if (i > 1) {
        delay(TEST_INTERVAL_MS);
      }
      Serial.print('[');
      Serial.print(i);
      Serial.print('/');
      Serial.print(count);
      Serial.print(F("] "));
      dispenseSelected();
    }
    Serial.println(F("Test done."));
  }
  endBusy();
}

static void handleCommand(const char* command) {
  command = skipSpaces(command);
  if (*command == '\0') {
    return;
  }
  const char letter = *command;
  const char* args = command + 1;

  switch (letter) {
    case 'g':
      commandGo(args);
      return;
    case 'o':
      commandSetOpenTime(args);
      return;
    case 't':
      commandTest(args);
      return;
  }

  // The remaining commands take no arguments.
  if (atEnd(args)) {
    switch (letter) {
      case 'h':
        printHelp();
        return;
      case 's':
        printStatus();
        return;
      case 'p':
        printBinTable();
        return;
      case 'n':
        if (requireIdle()) {
          pressNext();
        }
        return;
      case 'd':
        if (requireIdle()) {
          pressDispense();
        }
        return;
      case 'H':
        home();
        endBusy();
        return;
    }
  }
  Serial.println(F("Unknown command. Type h for help."));
}

static void pollSerial() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      // Reset the reader before running the command: a command that blocks
      // ends with endBusy(), which sets the reader state for what follows.
      const uint8_t len = lineLen;
      const bool skip = skipLine;
      lineLen = 0;
      skipLine = false;
      if (skip || len == 0) {
        continue;
      }
      if (len == sizeof(line)) {
        Serial.println(F("Unknown command. Type h for help."));
        continue;
      }
      line[len] = '\0';
      handleCommand(line);
    } else if (lineLen < sizeof(line)) {
      line[lineLen++] = c;
    }
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.print(F("Parts vending machine firmware v"));
  Serial.println(FW_VERSION);

  btnNext.begin();
  btnDispense.begin();
  carouselBegin();

  gateBegin();   // the gate is closed before anything turns
  home();
  endBusy();
  Serial.println(F("Type h for help."));
}

void loop() {
  pollSerial();

  // Both buttons are polled on every pass to keep their debounce state
  // current, but they only act when idle: in the error state they are ignored.
  const bool nextPressed = btnNext.pressed();
  const bool dispensePressed = btnDispense.pressed();
  if (state == State::Idle) {
    if (nextPressed) {
      pressNext();
    } else if (dispensePressed) {
      pressDispense();
    }
  }

  if (state == State::Error && millis() - errorPrintedMs >= ERROR_REPRINT_MS) {
    errorPrintedMs = millis();
    Serial.println(errorMessage);
  }
}
