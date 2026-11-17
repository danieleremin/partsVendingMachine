// Button bench test: Next on D4, Dispense on D5 (other side of each to GND)
// Serial Monitor: 9600 baud, line ending "Newline".
//
// Press the buttons. Each clean press should print exactly one line.
// The "bounces" number shows how many extra flickers the debounce filtered
// out; any value is fine, it just shows the debounce is doing its job.
//
// Commands:
//   r   reset the counters
//   h   show this help

const int NEXT_PIN     = 4;
const int DISPENSE_PIN = 5;
const unsigned long DEBOUNCE_MS = 50;

struct Button {
  const char*   name;
  int           pin;
  bool          stableState;   // HIGH = released (pull-up), LOW = pressed
  bool          lastRaw;
  unsigned long lastChange;    // time of last raw change
  unsigned long pressedAt;
  int           rawChanges;    // raw flickers since the last stable change
  long          presses;
};

Button buttons[] = {
  {"Next",     NEXT_PIN,     HIGH, HIGH, 0, 0, 0, 0},
  {"Dispense", DISPENSE_PIN, HIGH, HIGH, 0, 0, 0, 0},
};
const int BUTTON_COUNT = sizeof(buttons) / sizeof(buttons[0]);

void printHelp() {
  Serial.println(F("Press Next (D4) or Dispense (D5). Commands: r (reset counts), h (help)"));
}

void setup() {
  Serial.begin(9600);
  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].lastRaw = digitalRead(buttons[i].pin);
    buttons[i].stableState = buttons[i].lastRaw;
  }
  Serial.println(F("--- Button test ready ---"));
  printHelp();
  for (int i = 0; i < BUTTON_COUNT; i++) {
    if (buttons[i].stableState == LOW) {
      Serial.print(F("Warning: "));
      Serial.print(buttons[i].name);
      Serial.println(F(" reads pressed at startup. Check its wiring."));
    }
  }
}

void updateButton(Button &b) {
  bool raw = digitalRead(b.pin);
  unsigned long now = millis();

  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.lastChange = now;
    b.rawChanges++;
  }

  // Accept the new state only after it has held steady for DEBOUNCE_MS.
  if (raw != b.stableState && now - b.lastChange >= DEBOUNCE_MS) {
    b.stableState = raw;
    int bounces = b.rawChanges - 1;
    b.rawChanges = 0;

    if (b.stableState == LOW) {
      b.pressedAt = now;
      b.presses++;
      Serial.print(b.name);
      Serial.print(F(" pressed  (#"));
      Serial.print(b.presses);
      Serial.print(F(", bounces filtered: "));
      Serial.print(bounces);
      Serial.println(F(")"));
    } else {
      Serial.print(b.name);
      Serial.print(F(" released after "));
      Serial.print(now - b.pressedAt);
      Serial.print(F(" ms  (bounces filtered: "));
      Serial.print(bounces);
      Serial.println(F(")"));
    }
  }
}

void loop() {
  for (int i = 0; i < BUTTON_COUNT; i++) updateButton(buttons[i]);

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "r") {
      for (int i = 0; i < BUTTON_COUNT; i++) buttons[i].presses = 0;
      Serial.println(F("Counts reset."));
    } else if (line == "h") {
      printHelp();
    } else if (line.length() > 0) {
      Serial.println(F("Unknown command. Type h for help."));
    }
  }
}
