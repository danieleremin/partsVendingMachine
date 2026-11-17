// Piezo landing-sensor test: passive buzzer on A0, used as a vibration sensor
// Wiring: buzzer + to A0, buzzer - to GND, 1M ohm resistor across the two
// buzzer pins (100k works too, just less sensitive).
// Serial Monitor: 9600 baud, line ending "Newline".
//
// Commands:
//   l        live view: strongest hit every 250 ms (any key stops)
//   d        drop test: counts hits above the threshold (any key stops)
//   n<sec>   noise test: stepper + servo run, reports the strongest vibration
//   t<val>   set the hit threshold (0-1023), e.g. t40
//   h        show this help

#include <Servo.h>
#include <Stepper.h>

const int PIEZO_PIN = A0;
int threshold = 40;                      // reading that counts as a hit
const unsigned long DEAD_TIME_MS = 150;  // ignore ringing/bounces after a hit

const int SERVO_PIN    = 6;
const int CLOSED_ANGLE = 90;
const int OPEN_ANGLE   = 155;

const long STEPS_PER_REV = 2048;
const int  STEPPER_RPM   = 15;
const int IN1 = 8, IN2 = 9, IN3 = 10, IN4 = 11;
Stepper stepper(STEPS_PER_REV, IN1, IN3, IN2, IN4);
Servo gate;

bool stopRequested() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    return true;
  }
  return false;
}

void releaseCoils() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void printBar(int value) {
  for (int i = 0; i < value / 10 && i < 60; i++) Serial.print('#');
  Serial.println();
}

// ---------- live view ----------
void liveView() {
  Serial.println(F("Live view: strongest reading per 250 ms. Tap the tray. Any key stops."));
  while (!stopRequested()) {
    int peak = 0;
    unsigned long start = millis();
    while (millis() - start < 250) {
      int v = analogRead(PIEZO_PIN);
      if (v > peak) peak = v;
    }
    Serial.print(F("Peak: "));
    Serial.print(peak);
    Serial.print(peak >= threshold ? F("  HIT   ") : F("        "));
    printBar(peak);
  }
  Serial.println(F("Live view stopped."));
}

// ---------- drop test ----------
void dropTest() {
  Serial.print(F("Drop test, threshold "));
  Serial.print(threshold);
  Serial.println(F(". Drop parts onto the tray one at a time. Any key stops."));
  int hits = 0;
  while (!stopRequested()) {
    int v = analogRead(PIEZO_PIN);
    if (v >= threshold) {
      // Record the strongest reading during the dead time, then report.
      int peak = v;
      unsigned long start = millis();
      while (millis() - start < DEAD_TIME_MS) {
        int w = analogRead(PIEZO_PIN);
        if (w > peak) peak = w;
      }
      hits++;
      Serial.print(F("Hit #"));
      Serial.print(hits);
      Serial.print(F("  strength "));
      Serial.println(peak);
    }
  }
  Serial.print(F("Drop test stopped. Hits: "));
  Serial.println(hits);
}

// ---------- noise test ----------
void noiseTest(long seconds) {
  if (seconds < 1) seconds = 10;
  Serial.print(F("Noise test for "));
  Serial.print(seconds);
  Serial.println(F(" s. Don't drop anything. Stepper and servo will run."));

  unsigned long stepDelayUs = 60UL * 1000000UL / STEPS_PER_REV / STEPPER_RPM;
  unsigned long lastStep = micros();
  unsigned long lastServo = millis();
  bool open = false;
  gate.attach(SERVO_PIN);

  int peak = 0;
  long overThreshold = 0;
  unsigned long start = millis();
  while (millis() - start < (unsigned long)seconds * 1000UL) {
    if (micros() - lastStep >= stepDelayUs) {
      stepper.step(1);
      lastStep = micros();
    }
    if (millis() - lastServo >= 600) {
      open = !open;
      gate.write(open ? OPEN_ANGLE : CLOSED_ANGLE);
      lastServo = millis();
    }
    int v = analogRead(PIEZO_PIN);
    if (v > peak) peak = v;
    if (v >= threshold) overThreshold++;
    if (stopRequested()) break;
  }

  gate.write(CLOSED_ANGLE);
  delay(400);
  gate.detach();
  releaseCoils();

  Serial.print(F("Noise test done. Strongest vibration: "));
  Serial.print(peak);
  Serial.print(F(", readings over threshold: "));
  Serial.println(overThreshold);
}

void printHelp() {
  Serial.println(F("Commands: l (live), d (drop test), n<sec> (noise), t<val> (threshold), h"));
  Serial.print(F("Threshold: "));
  Serial.println(threshold);
}

void setup() {
  Serial.begin(9600);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  releaseCoils();
  stepper.setSpeed(STEPPER_RPM);
  Serial.println(F("--- Piezo test ready ---"));
  printHelp();
}

void loop() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  char cmd = line.charAt(0);
  long arg = line.substring(1).toInt();

  switch (cmd) {
    case 'l': liveView(); break;
    case 'd': dropTest(); break;
    case 'n': noiseTest(arg); break;
    case 't':
      if (arg < 1 || arg > 1023) Serial.println(F("Threshold must be 1-1023"));
      else { threshold = (int)arg; printHelp(); }
      break;
    case 'h': printHelp(); break;
    default:  Serial.println(F("Unknown command. Type h for help."));
  }
}
