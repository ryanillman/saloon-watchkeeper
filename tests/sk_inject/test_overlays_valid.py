import json
import unittest

import _path  # noqa: F401
from boatmodel import load_baseline
from overlay import Alarm, parse_overlay

B = load_baseline(_path.SKDIR / "baseline.json")
DIR = _path.SKDIR / "overlays"
NAMES = {
    "alarm-coolant", "alarm-fuel-low", "controller-dropout", "coolant-null", "glance-mixed",
    "key-off", "rpm-pickup-fails", "tanks", "three-bank", "two-engines", "victron-power",
    "warn-tank-low",
}


def load(name):
    return json.loads((DIR / f"{name}.json").read_text())


class OverlaysValid(unittest.TestCase):
    def test_names_are_the_twelve_old_scenarios(self):
        self.assertEqual({p.stem for p in DIR.glob("*.json")}, NAMES)

    def test_every_overlay_parses_against_the_baseline(self):
        for name in sorted(NAMES):
            with self.subTest(overlay=name):
                ov = parse_overlay(load(name), B)
                self.assertTrue(ov.description)
                self.assertGreater(ov.duration_ms, 0)

    def test_no_event_is_after_the_end(self):
        for name in sorted(NAMES):
            doc = load(name)
            for ev in doc["after"]:
                self.assertLessEqual(ev["atMs"], doc["durationMs"], f"{name}: {ev}")

    def test_alarm_overlays_use_alarm_steps(self):
        want = {"alarm-coolant": ("propulsion.port.temperature", "alarm"),
                "alarm-fuel-low": ("tanks.fuel.0.currentLevel", "alarm"),
                "warn-tank-low": ("tanks.freshWater.0.currentLevel", "warn")}
        for name, (path, state) in want.items():
            ov = parse_overlay(load(name), B)
            acts = ov.step(10 ** 9)
            self.assertIn(Alarm(path, state, None), acts, name)

    def test_three_bank_enables_its_group(self):
        self.assertEqual(parse_overlay(load("three-bank"), B).with_groups, {"three-bank"})
        self.assertEqual(parse_overlay(load("glance-mixed"), B).profile, "anchor")


if __name__ == "__main__":
    unittest.main()
