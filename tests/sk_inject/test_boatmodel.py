import datetime
import unittest

import _path  # noqa: F401
from boatmodel import TIDE_HIGH_M, TIDE_LOW_M, TIDE_PERIOD_S, TIDE_T0, load_baseline, tide_state

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

    def test_tide_times_are_in_the_future_in_signalk_form(self):
        import datetime
        v = vals()
        now = datetime.datetime.now(datetime.timezone.utc)
        for leaf in ("timeHigh", "timeLow"):
            s = v[f"environment.tide.{leaf}"]
            self.assertRegex(s, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z$")   # what the firmware parses: UTC, with the Z
            when = datetime.datetime.fromisoformat(s.replace("Z", "+00:00"))
            self.assertGreater(when, now)
            self.assertLess((when - now).total_seconds(), TIDE_PERIOD_S)

    def test_a_sounder_reports_all_three_depths_and_a_tide_plugin_its_heights(self):
        v = vals()
        for leaf in ("belowTransducer", "belowKeel", "belowSurface"):
            self.assertGreater(v[f"environment.depth.{leaf}"], 1.5)   # above the shallow zones
        for leaf in ("heightNow", "heightHigh", "heightLow"):
            self.assertIsInstance(v[f"environment.tide.{leaf}"], float)

    def test_now_resolves_to_iso_utc(self):
        s = vals()["navigation.datetime"]
        self.assertTrue(s.endswith("Z") and "T" in s)

    def test_environment_mode_is_day_by_default(self):
        # the auto palette follows environment.mode (app.c); routine boat = daytime
        self.assertEqual(vals()["environment.mode"], "day")

    def test_position_is_a_lat_lon_object(self):
        # the anchor plugins read navigation.position; the anchor is dropped where the boat is
        p = vals()["navigation.position"]
        self.assertEqual(set(p), {"latitude", "longitude"})
        self.assertTrue(-90 < p["latitude"] < 90 and -180 < p["longitude"] < 180)

    NAV_PATHS = ("navigation.headingMagnetic", "navigation.courseOverGroundTrue", "navigation.speedThroughWater",
                 "environment.wind.directionTrue", "environment.wind.speedApparent",
                 "environment.wind.angleApparent", "environment.wind.angleTrueWater")

    def test_navigation_inputs_for_derived_data(self):
        # signalk-derived-data (true wind, set and drift) needs these; angles are radians, speeds m/s
        for profile in ("underway", "anchor"):
            v = vals(profile)
            for p in self.NAV_PATHS:
                self.assertIsInstance(v[p], float, f"{profile} {p}")
        v = vals()
        self.assertAlmostEqual(v["navigation.headingMagnetic"], 0.820, delta=0.05)
        self.assertAlmostEqual(v["environment.wind.angleApparent"], 0.663, delta=0.05)
        self.assertAlmostEqual(v["navigation.speedThroughWater"], 3.29, delta=0.2)

    def test_true_wind_agrees_with_apparent_and_boat_speed(self):
        # the values must be a boat the derived-data plugin could really have computed
        import math
        for t in (0, 7000, 31000):
            v = vals(t=t)
            a, s = v["environment.wind.angleApparent"], v["environment.wind.speedApparent"]
            b = v["navigation.speedThroughWater"]
            twa = math.atan2(s * math.sin(a), s * math.cos(a) - b)
            self.assertAlmostEqual(v["environment.wind.angleTrueWater"], twa, delta=0.06)
            self.assertAlmostEqual(v["environment.wind.directionTrue"],
                                   v["navigation.headingMagnetic"] + twa, delta=0.08)

    def test_anchor_boat_is_not_moving_through_the_water(self):
        v = vals("anchor")
        self.assertEqual(v["navigation.speedThroughWater"], 0)
        self.assertAlmostEqual(v["environment.wind.angleTrueWater"], v["environment.wind.angleApparent"], delta=0.05)

    def test_course_and_autopilot_groups_are_opt_in(self):
        # a plotter course and an engaged autopilot are not part of every routine boat: an overlay turns them on
        course = ("navigation.course.calcValues.bearingTrackTrue", "navigation.course.calcValues.crossTrackError",
                  "navigation.course.calcValues.distance")
        ap = ("steering.autopilot.state", "steering.autopilot.target.headingTrue")
        v = vals()
        for p in course + ap:
            self.assertNotIn(p, v)
        on = B.default_on("underway") | {"course", "autopilot"}
        v = vals(on=on)
        for p in course + ap:
            self.assertIn(p, v)
        self.assertEqual(v["steering.autopilot.state"], "auto")
        self.assertAlmostEqual(v["navigation.course.calcValues.bearingTrackTrue"], 1.2217, delta=0.01)

    def test_current_is_an_object_with_set_and_drift(self):
        # signalk-derived-data publishes environment.current as one object
        c = vals()["environment.current"]
        self.assertEqual(set(c), {"setTrue", "drift"})
        self.assertAlmostEqual(c["setTrue"], 2.391, delta=0.01)

    def test_server_true_wind_speed_agrees_with_apparent_wind_and_boat_speed(self):
        import math
        v = vals()
        a, s, b = v["environment.wind.angleApparent"], v["environment.wind.speedApparent"], v["navigation.speedThroughWater"]
        self.assertAlmostEqual(v["environment.wind.speedTrue"], math.hypot(s * math.sin(a), s * math.cos(a) - b), delta=0.3)

    def test_variation_is_zero_so_true_heading_is_magnetic(self):
        # the display makes a true heading from magnetic plus variation; directionTrue above assumes none
        self.assertEqual(vals()["navigation.magneticVariation"], 0.0)

    def test_sog_is_published(self):
        self.assertAlmostEqual(vals()["navigation.speedOverGround"], 3.29, delta=0.2)

    def test_venus_dcpower_only_with_its_group(self):
        # the Summary uses dcPower for LOAD when present, so it is opt-in (docs/testing.md rows assume IN - BATTERY)
        self.assertNotIn("electrical.venus.dcPower", vals())
        on = B.default_on("underway") | {"venus-system"}
        self.assertIn("electrical.venus.dcPower", vals(on=on))

    def test_paths_and_groups(self):
        self.assertIn(PORT_RPM, B.paths())
        self.assertIn("electrical.batteries.280.voltage", B.paths())
        self.assertIn("three-bank", B.groups())
        self.assertIn("engine.port", B.groups())



UTC = datetime.timezone.utc


class Tide(unittest.TestCase):
    def test_tide_height_is_cosine_of_phase(self):
        at = lambda secs: tide_state(TIDE_T0 + datetime.timedelta(seconds=secs)).height_m
        self.assertAlmostEqual(at(0), TIDE_HIGH_M, places=6)
        self.assertAlmostEqual(at(TIDE_PERIOD_S / 2), TIDE_LOW_M, places=6)
        self.assertAlmostEqual(at(TIDE_PERIOD_S / 4), (TIDE_HIGH_M + TIDE_LOW_M) / 2, places=6)

    def _instants(self):
        return [TIDE_T0 + datetime.timedelta(minutes=37 * i + 5) for i in range(50)]

    def test_next_events_are_future(self):
        for now in self._instants():
            s = tide_state(now)
            self.assertGreater(s.next_high, now)
            self.assertGreater(s.next_low, now)

    def test_events_half_a_tide_apart(self):
        for now in self._instants():
            s = tide_state(now)
            self.assertAlmostEqual(abs((s.next_high - s.next_low).total_seconds()), TIDE_PERIOD_S / 2, places=3)

    def test_baseline_tide_values_move(self):
        v = vals()
        high = datetime.datetime.fromisoformat(v["environment.tide.timeHigh"].replace("Z", "+00:00"))
        self.assertGreater(high, datetime.datetime.now(UTC))
        self.assertTrue(TIDE_LOW_M <= v["environment.tide.heightNow"] <= TIDE_HIGH_M)
        # no wobble: two samples a second apart in model time differ only by the tide's own slope
        a = B.values(0.0, "underway", B.default_on("underway"), {})["environment.tide.heightNow"]
        b = B.values(1000.0, "underway", B.default_on("underway"), {})["environment.tide.heightNow"]
        self.assertAlmostEqual(a, b, places=3)


if __name__ == "__main__":
    unittest.main()
