# Parts Vending Machine — Proof-of-Concept Build Spec

This document specifies what to build for Phase 1 (interface/config definitions) and Phase 2 (Python host-side simulation) of a DIY parts vending machine. No hardware exists yet — everything here should run on a laptop. Give this whole document to a coding agent as its instructions.

## Project Context

A carousel-style small-parts dispenser (resistors, screws) with a servo-driven gate and an IR break-beam sensor to confirm a part actually dropped. It will eventually run on an Arduino-class microcontroller (or possibly a Raspberry Pi), controlling a stepper motor (carousel rotation) and a servo (gate open/close). Right now, only software is being built — no physical parts exist yet, so everything must be testable without hardware.

---

## Phase 1 — Interfaces and Configuration

### Deliverable 1: A C header as the single configuration source

Create one C header file (`config.h`) as the single source of truth for all machine constants. This is the file the future Arduino firmware will `#include` directly. Do not create a separate JSON/YAML file and do not hand-maintain a second copy of these values in Python — the Python simulation must instead parse `config.h` directly at runtime (see Deliverable 4), so the header stays the only place these numbers are ever edited.

Keep the header restricted to simple `#define` constants (or `const` declarations, pick one style and use it consistently) with no macros, conditionals, or C++-only syntax that a simple parser would struggle with. Each value should be defined on its own line so a line-based parser can find `NAME value` pairs reliably. One-line comments explaining each value are still expected, but keep them on the same line or the line directly above, in a consistent position, since the parser will need to skip them predictably.

The header must define:

- **Carousel geometry**
  - Number of bins (start at 12)
  - Bin angle (derived: 360° divided by bin count — note whether this is stored or computed)
  - Steps per full revolution of the stepper (placeholder value, to be corrected once real hardware is known — mark it clearly as provisional)
  - Steps per bin (derived from the above)

- **Gate/servo motion**
  - Closed angle (degrees)
  - Open angle (degrees)
  - Time allotted for the servo to complete its travel (milliseconds)

- **Dispense behavior**
  - Wait time after opening the gate before checking the sensor (milliseconds)
  - Maximum retry attempts per dispense request

- **Pin assignments** (placeholder values are fine — these exist so the names are settled before wiring happens)
  - Stepper driver: STEP pin, DIR pin, ENABLE pin
  - Servo signal pin
  - IR break-beam sensor: emitter pin (if applicable) and receiver/digital-input pin
  - Optional: status LED pin, manual test-trigger button pin

Document each value with a one-line comment explaining what it controls and, where relevant, what physical measurement will eventually replace the placeholder.

### Deliverable 2: A state machine diagram

Describe (as a diagram — Mermaid state diagram syntax is fine, embedded in this same markdown file or a companion file) the dispense cycle as an explicit sequence of named states:

`IDLE → ROTATING → GATE_OPENING → WAITING_FOR_SENSOR → GATE_CLOSING → SUCCESS or RETRY or FAIL → IDLE`

The `RETRY` path should loop back to `GATE_OPENING` (the carousel does not need to re-rotate on retry) up to the configured maximum retry count, after which the cycle ends in `FAIL`.

This diagram is the shared contract: the Python simulation in Phase 2 and the eventual firmware must both implement exactly these states and transitions, so behavior can be compared between them later.

### Deliverable 3: A header-parsing loader for Python

Write a small Python loader that reads `config.h` and exposes its values as ordinary Python variables/constants (or a config object) for the rest of the simulation to import. It should:
  - Locate `config.h` at a configurable path (default to it sitting alongside the Python package, but allow overriding the path).
  - Parse `#define NAME VALUE` (or `const` declaration) lines, extracting the name and value while ignoring comments and blank lines.
  - Convert numeric values to the correct Python type (int for pin numbers/counts, float for angles if they're ever non-integer).
  - Fail loudly with a clear error message if a value the simulation depends on is missing from the header, rather than silently defaulting — since the header is the only source of truth, a missing value is a configuration bug that should surface immediately.
  - Recompute any derived values (e.g. steps-per-bin from steps-per-revolution and bin count) in Python after loading the base constants, rather than expecting the header to contain pre-computed derived values, unless the header author prefers to store them explicitly — pick one approach and note it in the loader's docstring so it's unambiguous which values are raw and which are derived.

This loader is what makes the header the single source of truth in practice: nothing in the Python simulation should hardcode a value that also appears in `config.h`.

### Deliverable 4: A command protocol description

Specify a simple text-based command format for requesting a dispense, e.g. a single letter/command name followed by a bin number, terminated by a newline. This same protocol should work whether commands arrive from a keyboard/stdin (Phase 2), a serial connection (future firmware), or eventually a Raspberry Pi/web frontend. Define:
  - The exact syntax for a "dispense bin N" command
  - What response/acknowledgment format is sent back (e.g. success/fail, number of attempts used)
  - How malformed input or an out-of-range bin number should be handled

---

## Phase 2 — Python Host-Side Simulation

Build a small Python package/module that models the machine's control logic entirely in software, using the config and state machine from Phase 1. No hardware libraries, no serial ports required for this phase.

### Component 1: Carousel model

A representation of the carousel that tracks which bin is currently under the gate and can compute the rotation needed to reach a target bin, accounting for wraparound (e.g. moving from the last bin to bin 0 should take the short way around, not spin backward through every bin). It should reject or handle gracefully any request for a bin index outside the valid range. Its bin count must come from the Deliverable 3 header loader, not be passed in as a hardcoded literal, so changing `BIN_COUNT` in `config.h` is the only edit needed to reconfigure the simulation.

### Component 2: Gate model

A representation of the servo gate with an open/closed state. It doesn't need real timing simulated — open() and close() just need to flip a state flag that other components can inspect.

### Component 3: Sensor model

A simulated IR break-beam sensor whose "did a part drop" result is randomized according to a configurable success probability. This lets the simulation exercise the retry logic the same way the real screw hopper will occasionally under- or over-dispense. The probability should be an easily adjustable parameter, not buried in the class.

### Component 4: Dispense orchestration

A function or method that ties the three components together to execute exactly one full pass through the Phase 1 state machine for a given bin number:
1. Rotate the carousel to the requested bin.
2. Open the gate.
3. Query the sensor.
4. Close the gate.
5. If the sensor reported success, stop and report success along with the number of attempts used.
6. If not, and retries remain, repeat from step 2 (no re-rotation).
7. If retries are exhausted, report failure.

This function should depend only on the Carousel/Gate/Sensor interfaces described above (not on any concrete implementation details), so that real motor/servo/sensor drivers can later be substituted with no change to this logic.

### Component 5: Command-line harness

A small interactive script or REPL that reads commands in the Phase 1 protocol format (from stdin or a simple loop) and prints the resulting state transitions and final outcome for each dispense request. This is the substitute for physical interaction until hardware exists, and should be structured so that swapping stdin for a real serial connection later requires minimal changes.

### Component 6: Optional result logging

Each dispense attempt (bin number, number of attempts, success/failure, timestamp) can optionally be appended to a CSV file. This isn't required for the core simulation to work, but is useful later for tuning the sensor's success-probability parameter against real bench-test data, and is a natural first step toward inventory tracking if that's added later.

### Test coverage required

Write automated tests (pytest or unittest) covering at minimum:
- Rotating from the last bin to bin 0 computes the correct short-path rotation, not a full reverse loop
- A dispense that fails once but succeeds on retry reports the correct attempt count and a success result
- A dispense that fails on every attempt reports failure after exactly the configured number of retries, no more and no fewer
- Requesting a bin index outside the valid range is rejected/handled without crashing
- Two consecutive dispense requests for the same bin correctly compute zero rotation for the second one

### Explicit non-goals for this phase

- No actual GPIO, serial, or motor control code
- No firmware/Arduino code
- No UI beyond a plain text command-line harness
- Inventory tracking beyond basic CSV logging is optional and can be skipped

---

## Handoff Notes

Whoever implements this should treat `config.h` as the single source of truth — no magic numbers duplicated elsewhere in the Phase 2 code, and no separate Python constants file that could drift out of sync with the header. Every value the Python simulation needs should flow through the Deliverable 3 loader. The state machine diagram from Phase 1 should be implementable as a straightforward conditional/loop structure in Phase 2; it does not need a formal state-machine library.
