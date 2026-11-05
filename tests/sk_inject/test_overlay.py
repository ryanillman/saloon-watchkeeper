import unittest

import _path  # noqa: F401
from boatmodel import load_baseline
from overlay import (Alarm, Clear, GroupOff, GroupOn, Move, Overlay, OverlayError, Post, Put, Ramp,
                     Set, Silent, parse_overlay)

B = load_baseline(_path.SKDIR / "baseline.json")
RPM = "propulsion.port.revolutions"
TEMP = "propulsion.port.temperature"


def doc(*events, **kw):
    d = {"description": "t", "durationMs": 60000, "after": list(events)}
    d.update(kw)
    return d


class OverlayEngine(unittest.TestCase):
    def test_events_fire_once_in_order(self):
        ov = parse_overlay(doc(
            {"atMs": 5000, "op": "set", "path": RPM, "value": 20},
            {"atMs": 1000, "op": "null", "path": TEMP},
            {"atMs": 9000, "op": "silent", "path": RPM}), B)
        self.assertEqual(ov.step(500), [])
        self.assertEqual(ov.step(1000), [Set(TEMP, None)])
        self.assertEqual(ov.step(1000), [])
        self.assertEqual(ov.step(6000), [Set(RPM, 20)])
        self.assertEqual(ov.step(9000), [Silent(RPM)])
        self.assertEqual(ov.step(60000), [])

    def test_step_after_gap_fires_all_due(self):
        ov = parse_overlay(doc(
            {"atMs": 1000, "op": "group-off", "group": "engine.port"},
            {"atMs": 2000, "op": "group-on", "group": "engine.port"}), B)
        self.assertEqual(ov.step(30000), [GroupOff("engine.port"), GroupOn("engine.port")])

    def test_ramp_alarm_clear_parse(self):
        ov = parse_overlay(doc(
            {"atMs": 0, "op": "ramp", "path": RPM, "to": 40, "overMs": 10000},
            {"atMs": 10000, "op": "alarm", "path": TEMP, "state": "alarm", "holdMs": 30000},
            {"atMs": 40000, "op": "clear", "path": TEMP}), B)
        self.assertEqual(ov.step(40000), [Ramp(RPM, None, 40, 10000),
                                          Alarm(TEMP, "alarm", 30000), Clear(TEMP)])

    def test_profile_and_groups_and_duration(self):
        ov = parse_overlay(doc(profile="anchor", **{"with": ["three-bank"]}), B)
        self.assertEqual((ov.profile, ov.with_groups, ov.duration_ms), ("anchor", {"three-bank"}, 60000))

    def test_unknown_path_rejected(self):
        with self.assertRaises(OverlayError):
            parse_overlay(doc({"atMs": 0, "op": "set", "path": "nope.nothing", "value": 1}), B)

    def test_unknown_group_and_profile_rejected(self):
        with self.assertRaises(OverlayError):
            parse_overlay(doc({"atMs": 0, "op": "group-off", "group": "warp.drive"}), B)
        with self.assertRaises(OverlayError):
            parse_overlay(doc(profile="sailing"), B)

    def test_alarm_requires_valid_state(self):
        for ev in ({"atMs": 0, "op": "alarm", "path": TEMP},
                   {"atMs": 0, "op": "alarm", "path": TEMP, "state": "panic"}):
            with self.assertRaises(OverlayError):
                parse_overlay(doc(ev), B)

    def test_no_notification_paths_allowed(self):
        with self.assertRaises(OverlayError) as cm:
            parse_overlay(doc({"atMs": 0, "op": "set",
                               "path": "notifications.propulsion.port.temperature",
                               "value": {"state": "alarm"}}), B)
        self.assertIn("notifications", str(cm.exception))

    def test_post_put_move_parse(self):
        ov = parse_overlay(doc(
            {"atMs": 1000, "op": "post", "path": "/plugins/anchoralarm/dropAnchor", "body": {"radius": 45}},
            {"atMs": 1000, "op": "post", "path": "/plugins/anchoralarm/raiseAnchor"},
            {"atMs": 2000, "op": "put", "path": "navigation.anchor.maxRadius", "value": 45},
            {"atMs": 3000, "op": "move", "path": "navigation.position",
             "northM": 20, "eastM": -5, "overMs": 10000}), B)
        self.assertEqual(ov.step(3000), [
            Post("/plugins/anchoralarm/dropAnchor", {"radius": 45}),
            Post("/plugins/anchoralarm/raiseAnchor", None),
            Put("navigation.anchor.maxRadius", 45),
            Move("navigation.position", 20.0, -5.0, 10000)])

    def test_put_may_name_a_source(self):
        ov = parse_overlay(doc({"atMs": 0, "op": "put", "path": "navigation.anchor.maxRadius",
                                "value": 45, "source": "anchoralarm"}), B)
        self.assertEqual(ov.step(0), [Put("navigation.anchor.maxRadius", 45, "anchoralarm")])

    def test_post_must_target_a_plugin(self):
        for bad in ("/signalk/v1/api/vessels/self/navigation/position", "plugins/x/y", "/admin/restart"):
            with self.assertRaises(OverlayError, msg=bad):
                parse_overlay(doc({"atMs": 0, "op": "post", "path": bad}), B)

    def test_put_never_targets_notifications(self):
        with self.assertRaises(OverlayError):
            parse_overlay(doc({"atMs": 0, "op": "put", "path": "notifications.navigation.anchor",
                               "value": {"state": "alarm"}}), B)

    def test_put_needs_a_value(self):
        with self.assertRaises(OverlayError):
            parse_overlay(doc({"atMs": 0, "op": "put", "path": "navigation.anchor.maxRadius"}), B)

    def test_move_only_on_a_position_path(self):
        with self.assertRaises(OverlayError):
            parse_overlay(doc({"atMs": 0, "op": "move", "path": RPM,
                               "northM": 1, "eastM": 1, "overMs": 1000}), B)
        with self.assertRaises(OverlayError):
            parse_overlay(doc({"atMs": 0, "op": "move", "path": "navigation.position"}), B)

    def test_unknown_op_rejected(self):
        with self.assertRaises(OverlayError):
            parse_overlay(doc({"atMs": 0, "op": "explode", "path": RPM}), B)


if __name__ == "__main__":
    unittest.main()
