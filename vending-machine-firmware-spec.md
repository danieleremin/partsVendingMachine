# Parts Vending Machine — Firmware Specification (v1)

## 1. Overview

A small vending machine that dispenses loose parts (screws in the M3–M5 range and similarly sized items). Parts are stored in a **rotating carousel of 10 bins**, driven by a stepper motor. One bin at a time sits over a **dispense hole**, which is covered by a **servo-driven gate**. Opening the gate briefly lets a part fall out.

The machine has **no dispense detection**. The gate is opened for a calibrated amount of time per bin, and the result (0, 1, or occasionally 2 parts) is not verified. If nothing comes out, the user presses Dispense again.

The user interface is two buttons:

- **Next:** turns the carousel to the next bin.
- **Dispense:** opens the gate over the current bin for that bin's set time.

The Serial Monitor serves as the display and as a calibration interface.

## 2. Hardware

| Component | Part | Connection |
|---|---|---|
| Controller | Arduino Uno | USB for development |
| Carousel drive | 28BYJ-48 stepper (5 V, unipolar) | Via ULN2003 driver board |
| Stepper driver | ULN2003 board | IN1–IN4 on `D8`–`D11` |
| Gate | SG90 micro servo | Signal on `D6` |
| Home sensor | Digital hall effect sensor + magnet on the carousel | Signal on `D3` |
| Next button | Momentary push button | `D4` to GND |
| Dispense button | Momentary push button | `D5` to GND |
| LCD (later, optional) | 16×2 I2C LCD | SDA `A4`, SCL `A5` |

**Power:** A separate 5 V supply feeds the stepper driver, servo and sensor, with its ground shared with the Arduino. A 100 µF capacitor sits across 5 V and GND at the servo.

**Free pins:** `D2`, `D7`, `D12`, `D13`, `A0`–`A3`.

### 2.1 Pin map

| Constant | Pin | Mode | Active level |
|---|---|---|---|
| `PIN_STEP_IN1` | `D8` | Output | — |
| `PIN_STEP_IN2` | `D9` | Output | — |
| `PIN_STEP_IN3` | `D10` | Output | — |
| `PIN_STEP_IN4` | `D11` | Output | — |
| `PIN_SERVO` | `D6` | Servo signal | — |
| `PIN_HALL` | `D3` | `INPUT_PULLUP` | LOW = magnet present (confirm on hardware; make it a constant) |
| `PIN_BTN_NEXT` | `D4` | `INPUT_PULLUP` | LOW = pressed |
| `PIN_BTN_DISPENSE` | `D5` | `INPUT_PULLUP` | LOW = pressed |

**Stepper pin order:** with the Arduino `Stepper` library, the motor must be constructed as `Stepper(STEPS_PER_REV, IN1, IN3, IN2, IN4)`, i.e. pins `8, 10, 9, 11`. Any other order makes the motor buzz instead of turning.

## 3. PlatformIO project

### 3.1 `platformio.ini`

```ini
[env:uno]
platform = atmelavr
board = uno
framework = arduino
monitor_speed = 115200
lib_deps =
    arduino-libraries/Servo@^1.2.2
    arduino-libraries/Stepper@^1.1.3
```

If the board is an Uno R4, use `platform = renesas-ra` with `board = uno_r4_minima` or `board = uno_r4_wifi` instead.

### 3.2 Suggested layout

```
include/
  config.h      pins, tuning constants, bin table
src/
  main.cpp      setup(), loop(), serial command handling
  carousel.cpp  homing, position tracking, moves
  carousel.h
  gate.cpp      servo open/close cycle
  gate.h
  buttons.cpp   debounce and edge detection
  buttons.h
```

A single `main.cpp` is also acceptable for v1.

**Note for `.cpp` files:** unlike `.ino` sketches, functions must be declared before they are used, and each file needs `#include <Arduino.h>`.

## 4. Configuration (`config.h`)

All tunable values live in one place. Values marked "verified" have been bench-tested on this hardware.

| Constant | Value | Notes |
|---|---|---|
| `STEPS_PER_REV` | `2048` | Verified: holds position over many revolutions |
| `STEPPER_RPM` | `15` | Verified: normal moves |
| `HOMING_RPM` | `5` | Slower, for a repeatable home position |
| `BIN_COUNT` | `10` | |
| `HOME_OFFSET_STEPS` | `0` | Steps from the magnet trigger point to bin 0 aligned over the dispense hole. Calibrate on the built machine. |
| `HOMING_TIMEOUT_STEPS` | `STEPS_PER_REV * 12 / 10` | 1.2 revolutions |
| `GATE_CLOSED_DEG` | `90` | Tune on the real gate |
| `GATE_OPEN_DEG` | `155` | Tune on the real gate |
| `SERVO_TRAVEL_MS` | `300` | Time allowed for the servo to finish a move |
| `SETTLE_MS` | `200` | Pause after a carousel move before the gate opens |
| `DEBOUNCE_MS` | `50` | Verified in button bench test |
| `DEFAULT_OPEN_MS` | `150` | Starting gate open time for uncalibrated bins |
| `SERIAL_BAUD` | `115200` | |

### 4.1 Bin table

One entry per bin. Names are placeholders until bin contents are decided. Calibration only changes `openMs`.

```cpp
struct Bin {
  const char* name;
  uint16_t    openMs;   // how long the gate stays open
};

Bin bins[BIN_COUNT] = {
  {"Bin 0", 150}, {"Bin 1", 150}, {"Bin 2", 150}, {"Bin 3", 150}, {"Bin 4", 150},
  {"Bin 5", 150}, {"Bin 6", 150}, {"Bin 7", 150}, {"Bin 8", 150}, {"Bin 9", 150},
};
```

## 5. Behavior

### 5.1 Startup

1. Start Serial and print a banner with the firmware version.
2. Configure all pins. Set all four stepper outputs LOW (coils off).
3. **Close the gate:** attach the servo, write `GATE_CLOSED_DEG`, wait `SERVO_TRAVEL_MS`, detach.
4. **Home the carousel** (5.2).
5. On success, select bin 0, print its name, and enter the idle state.

### 5.2 Homing

- Homing always turns **forward** (the same direction as normal moves) at `HOMING_RPM`, one step at a time, reading the hall sensor after every step.
- **If the sensor is already active at the start,** first step forward until it reads inactive, so the trigger is always approached from the same side.
- Continue stepping until the sensor becomes active. That step is the **home reference**.
- Then move forward `HOME_OFFSET_STEPS` and set the position to bin 0 (`pos = 0`).
- **Timeout:** if the sensor does not become active within `HOMING_TIMEOUT_STEPS`, stop, turn the coils off, print `ERROR: homing failed (magnet not found)`, and enter the **error state**.

### 5.3 Position model

- `pos` is the carousel position in steps within one revolution, `0` to `STEPS_PER_REV - 1`, where `0` is bin 0 over the dispense hole.
- The target for bin `n` is `round(n × STEPS_PER_REV / BIN_COUNT)`, computed with integer math as `(2 * n * STEPS_PER_REV + BIN_COUNT) / (2 * BIN_COUNT)`. Never accumulate a fixed per-bin step count (204.8 is not a whole number, so error would build up).
- **All moves are forward-only:** `delta = (target - pos + STEPS_PER_REV) % STEPS_PER_REV`. This keeps gearbox backlash taken up in the same direction every time.
- After every move, turn all four coils off. The gearbox holds the carousel in place.

### 5.4 Automatic drift correction

- Moves are executed one step at a time, with the hall sensor read after each step.
- When the sensor goes from inactive to active during a normal move, that is the home reference. Set `pos = (STEPS_PER_REV - HOME_OFFSET_STEPS) % STEPS_PER_REV`, since the trigger point is `HOME_OFFSET_STEPS` before bin 0, and continue the move to the original target.
- If the correction is larger than 20 steps, print a warning with the size of the correction. This would indicate slipping or a loose magnet.

### 5.5 Next button

- On press: `selected = (selected + 1) % BIN_COUNT`.
- Move the carousel forward to the new bin.
- Print `Bin <n>: <name>` when the move finishes.

### 5.6 Dispense button

On press, run the dispense cycle on the currently selected bin:

1. If the carousel is not at the selected bin's position (for example after a serial `g` command), move there and wait `SETTLE_MS`.
2. Attach the servo and write `GATE_OPEN_DEG`.
3. Wait `bins[selected].openMs`. This is the calibrated open time, measured from the open command to the close command.
4. Write `GATE_CLOSED_DEG`, wait `SERVO_TRAVEL_MS`, then detach the servo.
5. Print `Dispensed from bin <n> (<name>), open <ms> ms`.

Pressing Dispense again on the same bin repeats the cycle. This is the user's retry if nothing came out.

### 5.7 Rules that always apply

- **Gate safety:** the carousel never moves unless the gate is closed and `SERVO_TRAVEL_MS` has passed since the close command. Track this as a software flag set by the gate code.
- **Busy lock:** button presses during a move or dispense cycle are ignored, not queued.
- **Servo idle:** the servo is detached whenever it is not moving, to prevent buzzing and jitter.
- **Coils idle:** stepper coils are off whenever the carousel is not moving.
- **Blocking is acceptable for v1:** moves and the dispense cycle may block, since buttons are ignored during them anyway.

### 5.8 Buttons

- Both buttons use `INPUT_PULLUP` and read LOW when pressed.
- Debounce: a new state is accepted only after the raw reading has been stable for `DEBOUNCE_MS`.
- Actions trigger on the press (HIGH-to-LOW transition), once per press. Holding a button does not repeat the action.

### 5.9 Error state

- Entered when homing fails.
- The carousel and gate do not move. Buttons are ignored.
- Serial commands still work, so `H` can retry homing.
- The error message is reprinted every 5 seconds until resolved.

## 6. Serial interface

`115200` baud, commands terminated by a newline. Unknown commands print `Unknown command. Type h for help.`

| Command | Action |
|---|---|
| `h` | List commands |
| `s` | Status: state, `pos`, selected bin, and the full bin table with open times |
| `n` | Same as pressing Next |
| `d` | Same as pressing Dispense |
| `g<n>` | Go to bin `n` (0–9) and select it, e.g. `g3` |
| `H` | Re-run homing |
| `o<n> <ms>` | Set bin `n`'s open time in ms (RAM only in v1), e.g. `o3 180` |
| `t<n> <count>` | Test-dispense bin `n` `count` times, 1.5 s apart, e.g. `t3 20` |
| `p` | Print the bin table as a C initializer, ready to paste into `config.h` |

All serial commands obey the busy lock and gate safety rules.

## 7. Calibration procedure

Open times are tuned per bin, since different parts slide out at different rates.

1. Fill the bin with its real parts.
2. Run `t<n> 20` and count the results: how many cycles gave 0, 1, and 2 parts.
3. If any gave 0 parts, raise the open time with `o<n> <ms>`. If any gave 2, lower it.
4. Repeat until the results are acceptable.
5. When all bins are done, run `p` and paste the output into `config.h`.

`HOME_OFFSET_STEPS` is calibrated once on the assembled machine: home, then adjust the value until bin 0 sits exactly over the dispense hole.

## 8. Acceptance tests for v1

- [ ] On power-up, the gate closes, the carousel homes, and `Bin 0: <name>` is printed.
- [ ] Homing with the magnet removed fails within about 1.2 revolutions and enters the error state.
- [ ] Ten Next presses step through all 10 bins and return to bin 0 aligned with the dispense hole.
- [ ] Twenty full revolutions of Next presses show no visible drift, and any drift correction stays under 20 steps.
- [ ] Dispense opens the gate for the bin's open time, then closes it and detaches the servo.
- [ ] Presses during a move or dispense are ignored.
- [ ] The carousel never moves while the gate is open.
- [ ] `o`, `t`, and `p` work as described.

## 9. Out of scope for v1

- Saving calibrated open times to EEPROM so they survive a power cycle.
- LCD display of the selected bin's name.
- Inventory tracking (dispense counts per bin).
- Dispense detection of any kind.
