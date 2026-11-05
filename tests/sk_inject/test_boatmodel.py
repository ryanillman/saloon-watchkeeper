import unittest

import _path  # noqa: F401
from boatmodel import load_baseline

B = load_baseline(_path.SKDIR / "baseline.json")
PORT_RPM = "propulsion.port.revolutions"
STBD_RPM = "propulsion.starboard.revolutions"


def vals(profile="underway", t=0.0, on=None, over=None):
    return B.values(t, profile, B.default_on(profile) if on is None else on, over or {})


class BoatModel(unittest.TestCase):
    def test_underway_has_both_engines_running(self):
        v = vals()
        self.assertGreater(v[PORT_RPM], 0)
        self.assertGreater(v[STBD_RPM], 0)

    def test_anchor_engines_off(self):
        v = vals("anchor")
        self.assertEqual(v[PORT_RPM], 0)
        self.assertEqual(v["propulsion.port.alternatorVoltage"], 0)
        self.assertGreater(v["propulsion.port.temperature"], 0)

    def test_group_off_zeroes_engine_port_together(self):
        on = B.default_on("underway") - {"engine.port"}
        v = vals(on=on)
        for leaf in ("revolutions", "oilPressure", "alternatorVoltage"):
            self.assertEqual(v[f"propulsion.port.{leaf}"], 0)
        self.assertGreater(v[STBD_RPM], 0)

    def test_override_wins_and_none_is_null(self):
        v = vals(over={PORT_RPM: 7.5, "tanks.fuel.0.currentLevel": None})
        self.assertEqual(v[PORT_RPM], 7.5)
        self.assertIsNone(v["tanks.fuel.0.currentLevel"])

    def test_wobble_bounded(self):
        row = next(r for r in B.rows if r.get("wobble"))
        for t in range(0, 60000, 500):
            v = vals(t=t)[row["path"]]
            drift = abs(row.get("drift", 0)) * (t / 60000.0)
            self.assertLessEqual(abs(v - row["nominal"]), row["wobble"] + drift + 1e-9)

    def test_three_bank_absent_unless_enabled(self):
        self.assertNotIn("electrical.batteries.280.voltage", vals())
        on = B.default_on("underway") | {"three-bank"}
        self.assertIn("electrical.batteries.280.voltage", vals(on=on))

    def test_now_resolves_to_iso_utc(self):
        s = vals()["navigation.datetime"]
        self.assertTrue(s.endswith("Z") and "T" in s)

    def test_paths_and_groups(self):
        self.assertIn(PORT_RPM, B.paths())
        self.assertIn("electrical.batteries.280.voltage", B.paths())
        self.assertIn("three-bank", B.groups())
        self.assertIn("engine.port", B.groups())


if __name__ == "__main__":
    unittest.main()
