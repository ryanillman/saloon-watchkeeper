import unittest

import _path  # noqa: F401
import zones
from zones import NoSuchState, default_zones, pick_target, zone_index

FUEL = [
    {"lower": 0, "upper": 0.1, "state": "emergency", "message": ""},
    {"lower": 0.1, "upper": 0.2, "state": "alarm", "message": ""},
    {"lower": 0.2, "upper": 0.3, "state": "alert", "message": ""},
    {"lower": 0.3, "upper": 1, "state": "nominal", "message": ""},
]
STATES = {"nominal", "alert", "warn", "alarm", "emergency"}


class PickTarget(unittest.TestCase):
    def test_pick_target_inside_zone(self):
        v = pick_target(FUEL, "alarm")
        self.assertEqual(FUEL[zone_index(FUEL, v)]["state"], "alarm")
        self.assertTrue(0.1 <= v < 0.2)

    def test_edges_lower_inclusive_upper_exclusive(self):
        self.assertEqual(zone_index(FUEL, 0.1), 1)
        self.assertEqual(zone_index(FUEL, 0.2), 2)
        self.assertEqual(zone_index(FUEL, 1.0), -1)

    def test_gap_does_not_hide_target(self):
        zs = [{"lower": 0, "upper": 1, "state": "nominal"},
              {"lower": 5, "upper": 6, "state": "alarm"}]
        self.assertEqual(zone_index(zs, 3), -1)
        self.assertEqual(zs[zone_index(zs, pick_target(zs, "alarm"))]["state"], "alarm")

    def test_open_ended_zone_gives_finite_target(self):
        zs = [{"upper": 10, "state": "nominal"}, {"lower": 10, "state": "alarm"}]
        v = pick_target(zs, "alarm")
        self.assertTrue(v == v and abs(v) != float("inf"))
        self.assertEqual(zone_index(zs, v), 1)
        v0 = pick_target(zs, "nominal")
        self.assertEqual(zone_index(zs, v0), 0)

    def test_overlap_first_match(self):
        zs = [{"lower": 0, "upper": 10, "state": "nominal"},
              {"lower": 5, "upper": 15, "state": "alarm"}]
        self.assertEqual(zone_index(zs, 7), 0)
        # the alarm zone's midpoint (10) is outside the nominal zone, so it is reachable
        self.assertEqual(zs[zone_index(zs, pick_target(zs, "alarm"))]["state"], "alarm")

    def test_no_such_state_lists_available(self):
        with self.assertRaises(NoSuchState) as cm:
            pick_target(FUEL, "warn")
        self.assertEqual(cm.exception.states, ["emergency", "alarm", "alert", "nominal"])


class Defaults(unittest.TestCase):
    PATHS = [
        "propulsion.port.temperature", "propulsion.starboard.temperature",
        "propulsion.port.oilPressure", "propulsion.port.revolutions",
        "propulsion.port.alternatorVoltage",
        "tanks.fuel.0.currentLevel", "tanks.freshWater.0.currentLevel",
        "tanks.blackWater.0.currentLevel", "tanks.wasteWater.0.currentLevel",
        "electrical.batteries.279.capacity.stateOfCharge",
        "electrical.batteries.279.voltage",
    ]

    def test_defaults_valid(self):
        for p in self.PATHS:
            meta = default_zones(p)
            self.assertIsNotNone(meta, p)
            zs = meta["zones"]
            self.assertTrue(set(z["state"] for z in zs) <= STATES, p)
            for a, b in zip(zs, zs[1:]):
                self.assertEqual(a["upper"], b["lower"], f"{p}: zones must be contiguous")
            # the server reads "<state>Method" from the meta (zones.js getMethod)
            self.assertEqual(meta["alarmMethod"], ["visual", "sound"], p)
            self.assertEqual(meta["emergencyMethod"], ["visual", "sound"], p)
            self.assertEqual(meta["warnMethod"], ["visual"], p)
            for z in zs:
                self.assertLess(z["lower"], z["upper"], p)

    def test_defaults_cover_migrated_alarms(self):
        def states(p):
            return {z["state"] for z in default_zones(p)["zones"]}
        self.assertIn("alarm", states("propulsion.port.temperature"))
        self.assertIn("warn", states("tanks.freshWater.0.currentLevel"))
        self.assertIn("alarm", states("tanks.fuel.0.currentLevel"))

    def test_baseline_value_is_nominal(self):
        import boatmodel
        b = boatmodel.load_baseline(_path.SKDIR / "baseline.json")
        v = b.values(0, "underway", b.default_on("underway"), {})
        for p in self.PATHS:
            meta = default_zones(p)
            if meta is None or not isinstance(v.get(p), (int, float)):
                continue
            z = meta["zones"][zone_index(meta["zones"], v[p])]
            self.assertEqual(z["state"], "nominal", f"{p}={v[p]} must not start in an alarm zone")

    def test_default_zones_none_for_unknown_path(self):
        self.assertIsNone(default_zones("navigation.speedOverGround"))

    def test_default_zones_returns_independent_copy(self):
        a = default_zones("propulsion.port.temperature")
        a["zones"][0]["state"] = "mutated"
        self.assertNotEqual(default_zones("propulsion.port.temperature")["zones"][0]["state"], "mutated")


if __name__ == "__main__":
    unittest.main()
