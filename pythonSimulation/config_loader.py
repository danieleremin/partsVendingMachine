"""Load machine constants from the firmware's config.h.

config.h is the single source of truth. This module parses it at runtime so
the simulation never hardcodes a value that also lives in the header.

Header location
    Default: ../partsVendingMachineMainCode/include/config.h relative to this
    file (where the PlatformIO firmware includes it from). Override by passing
    a path to load_config(), or by setting the VENDING_CONFIG_H environment
    variable.

Accepted syntax (anything else raises ConfigError)
    - blank lines
    - lines starting with // (comments)
    - /* ... */ block comments that start at the beginning of a line and end
      at the end of a line (nothing else on the opening/closing lines)
    - #pragma once
    - #define NAME VALUE [// trailing comment]
      where VALUE is an integer (-12, 3200) or decimal (1.5) literal.

Raw vs. derived values
    Raw (read from the header, int unless the literal has a decimal point):
        every name in REQUIRED, plus any other #define in the file.
    Derived (computed here, never stored in the header):
        BIN_ANGLE_DEG = 360 / BIN_COUNT                  (float)
        STEPS_PER_BIN = STEPS_PER_REV / BIN_COUNT        (float, may be non-integer)
    The firmware computes the same derived values the same way.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from pathlib import Path

ENV_VAR = "VENDING_CONFIG_H"
DEFAULT_PATH = (
    Path(__file__).resolve().parent.parent
    / "partsVendingMachineMainCode" / "include" / "config.h"
)

# Every value the simulation depends on. Missing any of these is a config bug.
REQUIRED = (
    "BIN_COUNT",
    "STEPS_PER_REV",
    "GATE_CLOSED_ANGLE",
    "GATE_OPEN_ANGLE",
    "GATE_TRAVEL_MS",
    "SENSOR_WAIT_MS",
    "MAX_ATTEMPTS",
    "SERIAL_BAUD",
    "CMD_MAX_LINE_LEN",
    "PIN_STEPPER_STEP",
    "PIN_STEPPER_DIR",
    "PIN_STEPPER_ENABLE",
    "PIN_IR_EMITTER",
    "PIN_IR_RECEIVER",
    "PIN_TEST_BUTTON",
    "PIN_SERVO",
    "PIN_STATUS_LED",
)

_DEFINE_RE = re.compile(
    r"^#define\s+(?P<name>[A-Za-z_]\w*)\s+(?P<value>\S+)\s*(?://.*)?$"
)
_INT_RE = re.compile(r"^-?\d+$")
_FLOAT_RE = re.compile(r"^-?(?:\d+\.\d*|\.\d+)$")


class ConfigError(Exception):
    """config.h is missing, malformed, or lacks a required value."""


@dataclass(frozen=True)
class Config:
    # Carousel geometry (raw)
    BIN_COUNT: int
    STEPS_PER_REV: int
    # Gate / servo (raw)
    GATE_CLOSED_ANGLE: int | float
    GATE_OPEN_ANGLE: int | float
    GATE_TRAVEL_MS: int
    # Dispense behavior (raw)
    SENSOR_WAIT_MS: int
    MAX_ATTEMPTS: int
    # Protocol (raw)
    SERIAL_BAUD: int
    CMD_MAX_LINE_LEN: int
    # Pins (raw)
    PIN_STEPPER_STEP: int
    PIN_STEPPER_DIR: int
    PIN_STEPPER_ENABLE: int
    PIN_IR_EMITTER: int
    PIN_IR_RECEIVER: int
    PIN_TEST_BUTTON: int
    PIN_SERVO: int
    PIN_STATUS_LED: int
    # Derived (computed in __post_init__)
    BIN_ANGLE_DEG: float = field(init=False)
    STEPS_PER_BIN: float = field(init=False)
    # Where the values came from, and every #define found (incl. extras).
    source: Path = field(default=DEFAULT_PATH, compare=False)
    raw: dict[str, int | float] = field(default_factory=dict, compare=False, repr=False)

    def __post_init__(self) -> None:
        self._validate()
        object.__setattr__(self, "BIN_ANGLE_DEG", 360 / self.BIN_COUNT)
        object.__setattr__(self, "STEPS_PER_BIN", self.STEPS_PER_REV / self.BIN_COUNT)

    def _validate(self) -> None:
        problems = []
        must_be_int = [n for n in REQUIRED if n not in ("GATE_CLOSED_ANGLE", "GATE_OPEN_ANGLE")]
        for name in must_be_int:
            if not isinstance(getattr(self, name), int):
                problems.append(f"{name} must be an integer, got {getattr(self, name)!r}")
        if not problems:
            for name in ("BIN_COUNT", "STEPS_PER_REV", "MAX_ATTEMPTS", "CMD_MAX_LINE_LEN", "SERIAL_BAUD"):
                if getattr(self, name) <= 0:
                    problems.append(f"{name} must be > 0, got {getattr(self, name)}")
            for name in ("GATE_TRAVEL_MS", "SENSOR_WAIT_MS"):
                if getattr(self, name) < 0:
                    problems.append(f"{name} must be >= 0, got {getattr(self, name)}")
        if problems:
            raise ConfigError(f"{self.source}: " + "; ".join(problems))


def _resolve_path(path: str | os.PathLike | None) -> Path:
    if path is not None:
        return Path(path)
    env = os.environ.get(ENV_VAR)
    return Path(env) if env else DEFAULT_PATH


def parse_header(path: str | os.PathLike) -> dict[str, int | float]:
    """Return every #define in the header as {NAME: int|float}."""
    path = Path(path)
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise ConfigError(
            f"config header not found: {path} "
            f"(pass a path or set {ENV_VAR})"
        ) from None

    values: dict[str, int | float] = {}
    in_block = False
    block_start = 0
    for lineno, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if in_block or line.startswith("/*"):
            if not in_block:
                block_start = lineno
            in_block = not line.endswith("*/") or line == "/*"
            continue
        if not line or line.startswith("//") or line == "#pragma once":
            continue
        m = _DEFINE_RE.match(line)
        if not m:
            raise ConfigError(f"{path}:{lineno}: unrecognized line: {raw_line!r}")
        name, value = m["name"], m["value"]
        if _INT_RE.match(value):
            parsed: int | float = int(value)
        elif _FLOAT_RE.match(value):
            parsed = float(value)
        else:
            raise ConfigError(
                f"{path}:{lineno}: {name} value {value!r} is not a plain number"
            )
        if name in values:
            raise ConfigError(f"{path}:{lineno}: {name} defined more than once")
        values[name] = parsed
    if in_block:
        raise ConfigError(f"{path}:{block_start}: unterminated /* comment")
    return values


def load_config(path: str | os.PathLike | None = None) -> Config:
    """Parse config.h and return a validated Config with derived values."""
    resolved = _resolve_path(path)
    values = parse_header(resolved)
    missing = [name for name in REQUIRED if name not in values]
    if missing:
        raise ConfigError(f"{resolved}: missing required value(s): {', '.join(missing)}")
    return Config(**{n: values[n] for n in REQUIRED}, source=resolved, raw=values)


if __name__ == "__main__":
    import sys

    cfg = load_config(sys.argv[1] if len(sys.argv) > 1 else None)
    print(f"# loaded from {cfg.source}")
    for name in REQUIRED:
        print(f"{name} = {getattr(cfg, name)}")
    print("# derived")
    for name in ("BIN_ANGLE_DEG", "STEPS_PER_BIN"):
        print(f"{name} = {getattr(cfg, name)}")
