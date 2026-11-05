import unittest

import _path  # noqa: F401
from boatmodel import load_baseline
from boatstate import BoatState
import math

from overlay import Alarm, Post, Put, parse_overlay

B = load_baseline(_path.SKDIR / "baseline.json")
POS = "navigation.position"
M_PER_DEG = 111320.0


def _north_m(p, lat0):
    return (p["latitude"] - lat0) * M_PER_DEG


def _east_m(p, lat0, lon0):
    return (p["longitude"] - lon0) * M_PER_DEG * math.cos(math.radians(lat0))


RPM = "propulsion.port.revolutions"
TEMP = "propulsion.port.temperature"
FUEL = "tanks.fuel.0.currentLevel"


def ov(*events, **kw):
    d = {"description": "t", "durationMs": 10000, "after": list(events)}
    d.update(kw)
    return parse_overlay(d, B)


def base(state, now):
    return B.values(now, state.profile, state.groups_on, {})


class BoatStateTests(unittest.TestCase):
    def test_baseline_only(self):
        s = BoatState(B)
        values, alarms, msgs = s.tick(0)
        self.assertGreater(values[RPM], 0)
        self.assertEqual((alarms, msgs), ([], []))

    def test_set_applies_then_releases_at_overlay_end(self):
        s = BoatState(B)
        s.start_overlay("t", ov({"atMs": 1000, "op": "set", "path": RPM, "value": 7.5}), 0)
        self.assertGreater(s.tick(500)[0][RPM], 7.5)
        self.assertEqual(s.tick(1000)[0][RPM], 7.5)
        values, _, msgs = s.tick(10000)
        self.assertNotEqual(values[RPM], 7.5)
        self.assertTrue(any("done" in m for m in msgs))

    def test_null_is_sent_as_none(self):
        s = BoatState(B)
        s.start_overlay("t", ov({"atMs": 0, "op": "null", "path": TEMP}), 0)
        self.assertIn(TEMP, s.tick(0)[0])
        self.assertIsNone(s.tick(0)[0][TEMP])

    def test_silent_path_omitted_then_restored(self):
        s = BoatState(B)
        s.start_overlay("t", ov({"atMs": 1000, "op": "silent", "path": TEMP}), 0)
        self.assertIn(TEMP, s.tick(500)[0])
        self.assertNotIn(TEMP, s.tick(1000)[0])
        self.assertIn(TEMP, s.tick(10000)[0])

    def test_ramp_starts_from_current_value(self):
        s = BoatState(B)
        s.start_overlay("t", ov({"atMs": 0, "op": "ramp", "path": FUEL, "to": 0.0, "overMs": 4000}), 0)
        start = base(s, 0)[FUEL]
        self.assertAlmostEqual(s.tick(0)[0][FUEL], start)
        self.assertAlmostEqual(s.tick(2000)[0][FUEL], start / 2)
        self.assertAlmostEqual(s.tick(4000)[0][FUEL], 0.0)
        self.assertAlmostEqual(s.tick(6000)[0][FUEL], 0.0)

    def test_group_off_and_restore_at_end(self):
        s = BoatState(B)
        s.start_overlay("t", ov({"atMs": 0, "op": "group-off", "group": "engine.port"}), 0)
        self.assertEqual(s.tick(0)[0][RPM], 0)
        self.assertGreater(s.tick(10000)[0][RPM], 0)

    def test_profile_and_with_apply_and_restore(self):
        s = BoatState(B)
        s.start_overlay("t", ov(profile="anchor", **{"with": ["three-bank"]}), 0)
        v = s.tick(0)[0]
        self.assertEqual(v[RPM], 0)
        self.assertIn("electrical.batteries.280.voltage", v)
        v = s.tick(10000)[0]
        self.assertGreater(v[RPM], 0)
        self.assertNotIn("electrical.batteries.280.voltage", v)

    def test_alarm_action_is_returned_for_the_daemon(self):
        s = BoatState(B)
        s.start_overlay("t", ov({"atMs": 2000, "op": "alarm", "path": TEMP, "state": "alarm", "holdMs": 3000}), 0)
        self.assertEqual(s.tick(1000)[1], [])
        owner_alarms = s.tick(2000)[1]
        self.assertEqual([a for _, a in owner_alarms], [Alarm(TEMP, "alarm", 3000)])

    def test_alarm_target_ramps_holds_then_releases(self):
        s = BoatState(B)
        owner = s.start_overlay("t", ov(), 0)
        start = base(s, 1000)[TEMP]
        s.drive_to(owner, TEMP, 380.0, 1000, ramp_ms=2000, hold_ms=3000)
        self.assertAlmostEqual(s.tick(1000)[0][TEMP], start, places=3)
        self.assertAlmostEqual(s.tick(2000)[0][TEMP], (start + 380.0) / 2, delta=2)
        self.assertEqual(s.tick(3000)[0][TEMP], 380.0)
        self.assertEqual(s.tick(5000)[0][TEMP], 380.0)
        self.assertNotEqual(s.tick(6100)[0][TEMP], 380.0)          # hold over: back to baseline

    def test_clear_removes_override(self):
        s = BoatState(B)
        owner = s.start_overlay("t", ov(), 0)
        s.drive_to(owner, TEMP, 380.0, 0, ramp_ms=0, hold_ms=None)
        self.assertEqual(s.tick(1000)[0][TEMP], 380.0)
        s.clear(TEMP)
        self.assertNotEqual(s.tick(1000)[0][TEMP], 380.0)

    def test_overlays_are_independent(self):
        s = BoatState(B)
        s.start_overlay("a", ov({"atMs": 0, "op": "set", "path": RPM, "value": 1.0}, durationMs=2000), 0)
        s.start_overlay("b", ov({"atMs": 0, "op": "null", "path": TEMP}, durationMs=8000), 0)
        v = s.tick(1000)[0]
        self.assertEqual(v[RPM], 1.0)
        self.assertIsNone(v[TEMP])
        v = s.tick(3000)[0]
        self.assertNotEqual(v[RPM], 1.0)                           # a ended
        self.assertIsNone(v[TEMP])                                 # b still running

    def test_driven_alarm_is_released_at_overlay_end(self):
        s = BoatState(B)
        owner = s.start_overlay("t", ov(), 0)
        s.drive_to(owner, TEMP, 380.0, 1000, ramp_ms=0, hold_ms=None)   # no hold: lasts to the end
        self.assertEqual(s.tick(9000)[0][TEMP], 380.0)
        self.assertNotEqual(s.tick(10000)[0][TEMP], 380.0)

    def test_drive_after_overlay_ended_is_refused(self):
        # an alarm step reads and writes zones for seconds; its overlay may end meanwhile.
        # The late drive must not leave an override nothing will ever release.
        s = BoatState(B)
        owner = s.start_overlay("t", ov(), 0)
        s.tick(10000)                                                      # overlay over
        self.assertFalse(s.drive_to(owner, TEMP, 380.0, 10500, ramp_ms=0, hold_ms=None))
        self.assertNotEqual(s.tick(11000)[0][TEMP], 380.0)
        self.assertTrue(s.drive_to("manual", TEMP, 380.0, 11000, ramp_ms=0, hold_ms=None))

    def test_move_ramps_position_offset_from_the_baseline(self):
        s = BoatState(B)
        s.start_overlay("t", ov({"atMs": 0, "op": "move", "path": POS,
                                 "northM": 100, "eastM": 0, "overMs": 10000}), 0)
        lat0, lon0 = base(s, 0)[POS]["latitude"], base(s, 0)[POS]["longitude"]
        p = s.tick(0)[0][POS]
        self.assertAlmostEqual(p["latitude"], lat0, places=9)
        p = s.tick(5000)[0][POS]
        self.assertAlmostEqual(_north_m(p, lat0), 50.0, places=3)
        self.assertAlmostEqual(_east_m(p, lat0, lon0), 0.0, places=3)
        p = s.tick(9999)[0][POS]
        self.assertAlmostEqual(_north_m(p, lat0), 100.0, delta=0.05)

    def test_second_move_starts_where_the_first_left_off(self):
        s = BoatState(B)
        s.start_overlay("t", ov(
            {"atMs": 0, "op": "move", "path": POS, "northM": 100, "eastM": 0, "overMs": 1000},
            {"atMs": 2000, "op": "move", "path": POS, "northM": 100, "eastM": 60, "overMs": 4000},
            durationMs=20000), 0)
        lat0, lon0 = base(s, 0)[POS]["latitude"], base(s, 0)[POS]["longitude"]
        for t in (0, 1000, 2000, 3000):                          # the daemon ticks every second
            s.tick(t)
        p = s.tick(4000)[0][POS]                                 # halfway through the second move
        self.assertAlmostEqual(_north_m(p, lat0), 100.0, places=3)
        self.assertAlmostEqual(_east_m(p, lat0, lon0), 30.0, places=3)

    def test_position_returns_to_the_baseline_when_the_overlay_ends(self):
        s = BoatState(B)
        s.start_overlay("t", ov({"atMs": 0, "op": "move", "path": POS,
                                 "northM": 100, "eastM": 0, "overMs": 1000}), 0)
        lat0 = base(s, 0)[POS]["latitude"]
        s.tick(0)
        self.assertGreater(_north_m(s.tick(5000)[0][POS], lat0), 99)
        self.assertAlmostEqual(_north_m(s.tick(10000)[0][POS], lat0), 0.0, places=6)

    def test_requests_are_returned_for_the_daemon(self):
        s = BoatState(B)
        s.start_overlay("t", ov(
            {"atMs": 1000, "op": "post", "path": "/plugins/anchoralarm/dropAnchor", "body": {"radius": 45}},
            {"atMs": 1000, "op": "put", "path": "navigation.anchor.maxRadius", "value": 45}), 0)
        self.assertEqual(s.tick(500)[1], [])
        got = [a for _, a in s.tick(1000)[1]]
        self.assertEqual(got, [Post("/plugins/anchoralarm/dropAnchor", {"radius": 45}),
                               Put("navigation.anchor.maxRadius", 45)])

    def test_manual_drive_without_overlay(self):
        s = BoatState(B)
        s.drive_to("manual", TEMP, 380.0, 0, ramp_ms=0, hold_ms=None)
        self.assertEqual(s.tick(500)[0][TEMP], 380.0)

    def test_status_summary(self):
        s = BoatState(B)
        s.start_overlay("t", ov(), 0)
        st = s.status(1000)
        self.assertEqual(st["profile"], "underway")
        self.assertEqual(st["overlays"], [{"name": "t", "elapsedMs": 1000, "durationMs": 10000}])


if __name__ == "__main__":
    unittest.main()
