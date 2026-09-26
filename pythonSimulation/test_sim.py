"""Tests for sim.py. Run from this folder: python -m unittest -v"""

import tempfile
import unittest
from pathlib import Path

from config_loader import load_config
from sim import Carousel, Gate, Machine, dispense

CFG = load_config()


class ScriptedSensor:
    """Returns pre-set detection results in order, then False forever."""

    def __init__(self, *results):
        self.results = list(results)
        self.calls = 0

    def part_detected(self):
        self.calls += 1
        return self.results.pop(0) if self.results else False


def make_machine(sensor, log_path=None):
    return Machine(CFG, Carousel(CFG), Gate(), sensor, log_path)


def run_lines(machine, *lines):
    out = []
    for line in lines:
        machine.handle_line(line, out.append)
    return out


class CarouselTests(unittest.TestCase):
    def test_last_bin_to_zero_takes_short_path(self):
        c = Carousel(CFG)
        c.rotate_to(CFG.BIN_COUNT - 1)
        self.assertEqual(c.rotate_to(0), 1)
        self.assertEqual(c.current_bin, 0)

    def test_zero_to_last_bin_goes_backward_one(self):
        c = Carousel(CFG)
        self.assertEqual(c.rotate_to(CFG.BIN_COUNT - 1), -1)

    def test_same_bin_twice_is_zero_rotation(self):
        c = Carousel(CFG)
        c.rotate_to(3)
        self.assertEqual(c.rotate_to(3), 0)

    def test_out_of_range_raises(self):
        c = Carousel(CFG)
        for bad in (-1, CFG.BIN_COUNT):
            with self.assertRaises(ValueError):
                c.rotate_to(bad)
        self.assertEqual(c.current_bin, 0)


class DispenseTests(unittest.TestCase):
    def test_fail_then_success_on_retry(self):
        gate = Gate()
        states = []
        result = dispense(Carousel(CFG), gate, ScriptedSensor(False, True), 2,
                          CFG.MAX_ATTEMPTS, states.append)
        self.assertTrue(result.success)
        self.assertEqual(result.attempts, 2)
        self.assertFalse(gate.is_open)
        self.assertEqual(states, [
            "ROTATING",
            "GATE_OPENING", "WAITING_FOR_SENSOR", "GATE_CLOSING", "RETRY",
            "GATE_OPENING", "WAITING_FOR_SENSOR", "GATE_CLOSING", "SUCCESS",
            "IDLE",
        ])

    def test_always_failing_stops_at_max_attempts(self):
        sensor = ScriptedSensor()
        states = []
        result = dispense(Carousel(CFG), Gate(), sensor, 0, CFG.MAX_ATTEMPTS, states.append)
        self.assertFalse(result.success)
        self.assertEqual(result.attempts, CFG.MAX_ATTEMPTS)
        self.assertEqual(sensor.calls, CFG.MAX_ATTEMPTS)
        self.assertEqual(states.count("GATE_OPENING"), CFG.MAX_ATTEMPTS)
        self.assertEqual(states.count("RETRY"), CFG.MAX_ATTEMPTS - 1)
        self.assertEqual(states[-2:], ["FAIL", "IDLE"])

    def test_out_of_range_bin_rejected_before_moving(self):
        sensor = ScriptedSensor(True)
        with self.assertRaises(ValueError):
            dispense(Carousel(CFG), Gate(), sensor, CFG.BIN_COUNT, CFG.MAX_ATTEMPTS)
        self.assertEqual(sensor.calls, 0)

    def test_consecutive_same_bin_needs_zero_rotation(self):
        carousel = Carousel(CFG)
        moves = []
        original = carousel.rotate_to
        carousel.rotate_to = lambda b: moves.append(original(b)) or moves[-1]
        for _ in range(2):
            dispense(carousel, Gate(), ScriptedSensor(True), 5, CFG.MAX_ATTEMPTS)
        self.assertEqual(moves, [5, 0])


class ProtocolTests(unittest.TestCase):
    def test_dispense_reply_is_last_line(self):
        out = run_lines(make_machine(ScriptedSensor(False, True)), "d 3\r\n")
        self.assertEqual(out[0], "STATE ROTATING")
        self.assertEqual(out[-2:], ["STATE IDLE", "OK D 3 2"])

    def test_fail_reply(self):
        out = run_lines(make_machine(ScriptedSensor()), "D 1")
        self.assertEqual(out[-1], f"FAIL D 1 {CFG.MAX_ATTEMPTS}")

    def test_errors_and_ping(self):
        m = make_machine(ScriptedSensor())
        out = run_lines(m, f"D {CFG.BIN_COUNT}", "D -1", "D x", "D", "D 1 2",
                        "X 1", "", "p", "D " + "1" * CFG.CMD_MAX_LINE_LEN)
        self.assertEqual(out, [
            f"ERR BIN_RANGE {CFG.BIN_COUNT}",
            "ERR BAD_ARG D -1",
            "ERR BAD_ARG D x",
            "ERR BAD_ARG D",
            "ERR BAD_ARG D 1 2",
            "ERR UNKNOWN_CMD X",
            "OK P",
            "ERR LINE_TOO_LONG",
        ])
        self.assertEqual(m.carousel.current_bin, 0)


class LoggingTests(unittest.TestCase):
    def test_csv_log_appends_rows(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "log.csv"
            run_lines(make_machine(ScriptedSensor(True, True), path), "D 1", "D 2")
            lines = path.read_text().splitlines()
        self.assertEqual(lines[0], "timestamp,bin,attempts,result")
        self.assertTrue(lines[1].endswith(",1,1,OK"))
        self.assertTrue(lines[2].endswith(",2,1,OK"))


if __name__ == "__main__":
    unittest.main()
