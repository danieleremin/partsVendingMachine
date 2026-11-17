// IR beam bench test: KY-005 emitter on D7 (via 220 ohm), IR receiver OUT on D2
// Serial Monitor: 9600 baud, line ending "Newline".
//
// Commands:
//   l        live view: beam strength every 250 ms, in windows (any key to stop)
//   d        drop test: counts objects falling through the beam (any key to stop)
//   n<sec>   noise test: stepper + servo run while the beam is watched, e.g. n20
//   b<us>    set burst length in microseconds, e.g. b400
//   g<us>    set gap length in microseconds, e.g. g2000
//   w<ms>    set beam window length, e.g. w1500
//   r<ms>    set rest between windows, e.g. r1000
//   h        show this help
//
// How the beam works: the receiver only sees IR flickering at 38 kHz, and it
// starts ignoring a signal that stays on constantly. So each check turns the
// 38 kHz signal on briefly, reads the receiver, then turns it off again.
// Receiver output: LOW = beam seen, HIGH = beam blocked.

#include <Servo.h>
#include <Stepper.h>

const int IR_LED_PIN   = 7;
const int RECEIVER_PIN = 2;
const unsigned int CARRIER_HZ = 38000;
// Adjustable from Serial Monitor with b<us> and g<us>.
// Remote-control receivers dislike long or frequent bursts: if they arrive
// too often, the receiver treats them as noise and turns its own gain down.
unsigned int burstUs = 400;   // signal on before reading
unsigned int gapUs   = 2000;  // signal off between checks

// A drop must block this many checks in a row to count (filters out blips),
// and the beam must be clear this many checks in a row before the next drop.
const int MIN_BLOCKED = 2;
const int MIN_CLEAR   = 5;

const int SERVO_PIN    = 6;
const int CLOSED_ANGLE = 90;
const int OPEN_ANGLE   = 155;

const long STEPS_PER_REV = 2048;
const int  STEPPER_RPM   = 15;
const int IN1 = 8, IN2 = 9, IN3 = 10, IN4 = 11;
Stepper stepper(STEPS_PER_REV, IN1, IN3, IN2, IN4);
Servo gate;

// The receiver's output can lag the burst by a few hundred microseconds and
// can end early, so instead of one read at the end of the burst, watch the
// pin for the whole burst plus a short tail. Any LOW at all means "seen".
const unsigned int TAIL_US = 300;

bool beamBlocked() {
  bool seen = false;
  unsigned long t0 = micros();
  tone(IR_LED_PIN, CARRIER_HZ);
  while (micros() - t0 < burstUs) {
    if (digitalRead(RECEIVER_PIN) == LOW) seen = true;
  }
  noTone(IR_LED_PIN);
  while (micros() - t0 < (unsigned long)burstUs + TAIL_US) {
    if (digitalRead(RECEIVER_PIN) == LOW) seen = true;
  }
  if (gapUs > TAIL_US) delayMicroseconds(gapUs - TAIL_US);
  return !seen;
}

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

// ---------- beam windows ----------
// The receiver treats a long, regular burst train as interference and starts
// suppressing it after about two seconds. So the beam only runs in short
// windows, with a rest between them so the receiver can reset. In the real
// machine the window is the moment after the gate opens.
unsigned long windowMs = 1500;  // beam active
unsigned long restMs   = 1000;  // beam off

// Rest with the beam off. Returns true if a key was pressed (stop).
bool restBeam() {
  noTone(IR_LED_PIN);
  unsigned long t = millis();
  while (millis() - t < restMs) {
    if (stopRequested()) return true;
  }
  return false;
}

// ---------- live view ----------
void liveView() {
  Serial.println(F("Live view. Each line covers 250 ms of an active window. Any key stops."));
  while (true) {
    Serial.println(F("-- window --"));
    unsigned long wStart = millis();
    while (millis() - wStart < windowMs) {
      long seen = 0, total = 0;
      unsigned long start = millis();
      while (millis() - start < 250) {
        if (!beamBlocked()) seen++;
        total++;
      }
      int pct = (int)(seen * 100 / total);
      Serial.print(F("Beam seen: "));
      Serial.print(pct);
      Serial.print(F("%  "));
      for (int i = 0; i < pct / 5; i++) Serial.print('#');
      Serial.println();
      if (stopRequested()) { Serial.println(F("Live view stopped.")); return; }
    }
    if (restBeam()) break;
  }
  Serial.println(F("Live view stopped."));
}

// ---------- drop test ----------
void dropTest() {
  Serial.println(F("Drop test. Drop a part only while ARMED is showing. Any key stops."));

  int drops = 0, blips = 0;
  bool stop = false;

  while (!stop) {
    Serial.println(F("ARMED"));
    int blockedRun = 0, clearRun = MIN_CLEAR;
    unsigned long blockStart = 0;
    unsigned long wStart = millis();

    while (millis() - wStart < windowMs) {
      if (stopRequested()) { stop = true; break; }
      if (beamBlocked()) {
        if (blockedRun == 0) blockStart = millis();
        blockedRun++;
        clearRun = 0;
      } else {
        clearRun++;
        if (blockedRun > 0 && clearRun >= MIN_CLEAR) {
          if (blockedRun >= MIN_BLOCKED) {
            drops++;
            Serial.print(F("  Drop #"));
            Serial.print(drops);
            Serial.print(F("  blocked for "));
            Serial.print(millis() - blockStart);
            Serial.println(F(" ms"));
          } else {
            blips++;
            Serial.println(F("  (blip ignored)"));
          }
          blockedRun = 0;
        }
      }
    }
    if (blockedRun > 0 && !stop) {
      Serial.println(F("  Beam still blocked at end of window"));
    }
    if (stop) break;
    Serial.println(F("resting..."));
    if (restBeam()) break;
  }

  Serial.print(F("Drop test stopped. Drops: "));
  Serial.print(drops);
  Serial.print(F(", blips: "));
  Serial.println(blips);
}

// ---------- noise test ----------
void noiseTest(long seconds) {
  if (seconds < 1) seconds = 10;
  Serial.print(F("Noise test for "));
  Serial.print(seconds);
  Serial.println(F(" s. Keep the beam clear. Stepper and servo will run."));

  unsigned long stepDelayUs = 60UL * 1000000UL / STEPS_PER_REV / STEPPER_RPM;
  unsigned long lastStep = micros();
  unsigned long lastServo = millis();
  bool open = false;
  gate.attach(SERVO_PIN);

  long checks = 0, falseBlocks = 0;
  unsigned long start = millis();
  unsigned long phaseStart = millis();
  bool beamOn = true;

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
    if (beamOn) {
      if (beamBlocked()) falseBlocks++;
      checks++;
      if (millis() - phaseStart >= windowMs) { beamOn = false; noTone(IR_LED_PIN); phaseStart = millis(); }
    } else if (millis() - phaseStart >= restMs) {
      beamOn = true; phaseStart = millis();
    }
    if (stopRequested()) break;
  }

  gate.write(CLOSED_ANGLE);
  delay(400);
  gate.detach();
  releaseCoils();

  Serial.print(F("Noise test done. Checks: "));
  Serial.print(checks);
  Serial.print(F(", false blocks: "));
  Serial.println(falseBlocks);
  if (falseBlocks == 0) {
    Serial.println(F("No false detections. Noise is not a problem."));
  } else {
    Serial.println(F("False detections found. Try the 100 ohm resistor on the receiver's VCC."));
  }
}

void printHelp() {
  Serial.println(F("Commands: l, d, n<sec>, b<us>, g<us>, w<ms>, r<ms>, h"));
  Serial.print(F("Burst "));
  Serial.print(burstUs);
  Serial.print(F(" us, gap "));
  Serial.print(gapUs);
  Serial.print(F(" us, window "));
  Serial.print(windowMs);
  Serial.print(F(" ms, rest "));
  Serial.print(restMs);
  Serial.println(F(" ms"));
}

void setup() {
  Serial.begin(9600);
  pinMode(RECEIVER_PIN, INPUT_PULLUP);
  pinMode(IR_LED_PIN, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  releaseCoils();
  stepper.setSpeed(STEPPER_RPM);
  Serial.println(F("--- Beam test ready ---"));
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
    case 'b':
      if (arg < 100 || arg > 5000) Serial.println(F("Burst must be 100-5000 us"));
      else { burstUs = (unsigned int)arg; printHelp(); }
      break;
    case 'g':
      if (arg < 100 || arg > 16000) Serial.println(F("Gap must be 100-16000 us"));
      else { gapUs = (unsigned int)arg; printHelp(); }
      break;
    case 'w':
      if (arg < 100 || arg > 10000) Serial.println(F("Window must be 100-10000 ms"));
      else { windowMs = arg; printHelp(); }
      break;
    case 'r':
      if (arg < 0 || arg > 10000) Serial.println(F("Rest must be 0-10000 ms"));
      else { restMs = arg; printHelp(); }
      break;
    case 'h': printHelp(); break;
    default:  Serial.println(F("Unknown command. Type h for help."));
  }
}
