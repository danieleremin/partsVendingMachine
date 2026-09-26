"""Host-side simulation of the parts vending machine.

Implements the dispense state machine (docs/state-machine.md) and the text
command protocol (docs/protocol.md) with software models of the carousel,
gate and IR sensor. All machine constants come from config.h via
config_loader; nothing here duplicates them.

Usage:
    python sim.py [--config PATH] [--p 0.85] [--seed N] [--log results.csv]
Then type protocol commands, e.g. `D 3`, `P`. Ctrl+D / Ctrl+Z ends input.
"""

from __future__ import annotations

import argparse
import csv
import random
import re
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable, Iterable

from config_loader import Config, load_config

# Simulation-only knob (not a machine constant, so it isn't in config.h).
DEFAULT_SUCCESS_PROBABILITY = 0.85

_BIN_TOKEN_RE = re.compile(r"^[0-9]+$")


# --- Component models --------------------------------------------------------

class Carousel:
    """Tracks which bin is under the gate. Bin count comes from config.h."""

    def __init__(self, cfg: Config):
        self.bin_count = cfg.BIN_COUNT
        self.current_bin = 0

    def rotate_to(self, target_bin: int) -> int:
        """Move to target_bin by the shortest path.

        Returns the signed number of bins moved (+ forward, - backward,
        0 if already there). A half-turn tie goes forward.
        """
        if not 0 <= target_bin < self.bin_count:
            raise ValueError(f"bin {target_bin} out of range 0..{self.bin_count - 1}")
        delta = (target_bin - self.current_bin) % self.bin_count
        if delta > self.bin_count // 2:
            delta -= self.bin_count
        self.current_bin = target_bin
        return delta


class Gate:
    """Servo gate reduced to an open/closed flag."""

    def __init__(self):
        self.is_open = False

    def open(self) -> None:
        self.is_open = True

    def close(self) -> None:
        self.is_open = False


class RandomSensor:
    """IR break-beam model: reports a drop with probability success_probability."""

    def __init__(self, success_probability: float = DEFAULT_SUCCESS_PROBABILITY,
                 rng: random.Random | None = None):
        self.success_probability = success_probability
        self.rng = rng or random.Random()

    def part_detected(self) -> bool:
        return self.rng.random() < self.success_probability


# --- Dispense orchestration --------------------------------------------------

@dataclass(frozen=True)
class DispenseResult:
    bin: int
    attempts: int
    success: bool


def dispense(carousel, gate, sensor, bin_index: int, max_attempts: int,
             on_state: Callable[[str], None] = lambda state: None) -> DispenseResult:
    """Run one pass of the dispense state machine for bin_index.

    Uses only carousel.bin_count / carousel.rotate_to(), gate.open() /
    gate.close() and sensor.part_detected(), so real drivers can be swapped
    in. on_state is called with each state name as it is entered.
    Raises ValueError for an out-of-range bin before anything moves.
    """
    if not 0 <= bin_index < carousel.bin_count:
        raise ValueError(f"bin {bin_index} out of range 0..{carousel.bin_count - 1}")

    on_state("ROTATING")
    carousel.rotate_to(bin_index)

    attempts = 0
    while True:
        on_state("GATE_OPENING")
        attempts += 1
        gate.open()
        on_state("WAITING_FOR_SENSOR")
        detected = sensor.part_detected()
        on_state("GATE_CLOSING")
        gate.close()
        if detected:
            on_state("SUCCESS")
            success = True
            break
        if attempts >= max_attempts:
            on_state("FAIL")
            success = False
            break
        on_state("RETRY")

    on_state("IDLE")
    return DispenseResult(bin_index, attempts, success)


# --- Result logging ----------------------------------------------------------

def log_result(path: Path, result: DispenseResult) -> None:
    """Append one dispense outcome to a CSV file, writing a header if new."""
    new_file = not path.exists() or path.stat().st_size == 0
    with path.open("a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        if new_file:
            writer.writerow(["timestamp", "bin", "attempts", "result"])
        writer.writerow([
            datetime.now().isoformat(timespec="seconds"),
            result.bin,
            result.attempts,
            "OK" if result.success else "FAIL",
        ])


# --- Command protocol --------------------------------------------------------

class Machine:
    """Protocol front end: turns command lines into state traces and replies."""

    def __init__(self, cfg: Config, carousel, gate, sensor, log_path: Path | None = None):
        self.cfg = cfg
        self.carousel = carousel
        self.gate = gate
        self.sensor = sensor
        self.log_path = log_path

    def handle_line(self, line: str, write: Callable[[str], None]) -> None:
        """Process one command line, sending each reply line through write()."""
        line = line.rstrip("\r\n")
        if len(line) > self.cfg.CMD_MAX_LINE_LEN:
            write("ERR LINE_TOO_LONG")
            return
        fields = line.split()
        if not fields:
            return

        cmd = fields[0].upper()
        if cmd == "P":
            write("OK P" if len(fields) == 1 else f"ERR BAD_ARG {line.strip()}")
        elif cmd == "D":
            if len(fields) != 2 or not _BIN_TOKEN_RE.match(fields[1]):
                write(f"ERR BAD_ARG {line.strip()}")
                return
            bin_index = int(fields[1])
            if bin_index >= self.cfg.BIN_COUNT:
                write(f"ERR BIN_RANGE {bin_index}")
                return
            result = dispense(self.carousel, self.gate, self.sensor, bin_index,
                              self.cfg.MAX_ATTEMPTS,
                              on_state=lambda s: write(f"STATE {s}"))
            if self.log_path:
                log_result(self.log_path, result)
            status = "OK" if result.success else "FAIL"
            write(f"{status} D {result.bin} {result.attempts}")
        else:
            write(f"ERR UNKNOWN_CMD {fields[0]}")

    def run(self, lines: Iterable[str], write: Callable[[str], None]) -> None:
        """Serve commands until lines is exhausted (stdin now, serial later)."""
        for line in lines:
            self.handle_line(line, write)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--config", help="path to config.h (default: firmware include/)")
    parser.add_argument("--p", type=float, default=DEFAULT_SUCCESS_PROBABILITY,
                        help="sensor success probability per attempt (default: %(default)s)")
    parser.add_argument("--seed", type=int, help="random seed for repeatable runs")
    parser.add_argument("--log", type=Path, help="append results to this CSV file")
    args = parser.parse_args(argv)

    cfg = load_config(args.config)
    machine = Machine(cfg, Carousel(cfg), Gate(),
                      RandomSensor(args.p, random.Random(args.seed)), args.log)
    print(f"# {cfg.BIN_COUNT} bins, MAX_ATTEMPTS={cfg.MAX_ATTEMPTS}, p={args.p}. "
          f"Commands: D <bin>, P", file=sys.stderr)
    machine.run(sys.stdin, lambda s: print(s, flush=True))


if __name__ == "__main__":
    main()
