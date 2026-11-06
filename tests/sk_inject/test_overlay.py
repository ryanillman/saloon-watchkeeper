import unittest

import _path  # noqa: F401
from boatmodel import load_baseline
from overlay import (Alarm, Clear, GroupOff, GroupOn, Move, Notify, Overlay, OverlayError, Post, Put, Ramp,
                     Set, Silent, notify_delta, parse_overlay)

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

    def test_notify_parses_with_and_without_a_position(self):
        ov = parse_overlay(doc({"atMs": 10, "op": "notify", "nature": "mob", "state": "emergency", "position": [41.5, -70.25]},
                               {"atMs": 20, "op": "notify", "nature": "fire", "state": "emergency", "message": "GALLEY FIRE"},
                               {"atMs": 30, "op": "notify", "nature": "mob", "state": "normal"}), B)
        got = ov.step(30)
        self.assertEqual(got[0], Notify("mob", "emergency", None, (41.5, -70.25)))
        self.assertEqual(got[1], Notify("fire", "emergency", "GALLEY FIRE", None))
        self.assertEqual(got[2], Notify("mob", "normal", None, None))

    def test_notify_rejects_bad_nature_state_and_position(self):
        bad = [{"nature": "propulsion.port", "state": "emergency"},
               {"nature": "mob", "state": "alarm"},
               {"nature": "mob", "state": "emergency", "position": [95, 0]},
               {"nature": "mob", "state": "emergency", "position": [41.5]},
               {"nature": "mob", "state": "emergency", "position": "here"},
               {"state": "emergency"}]
        for extra in bad:
            with self.subTest(extra=extra), self.assertRaises(OverlayError):
                parse_overlay(doc({"atMs": 0, "op": "notify", **extra}), B)

    def test_notify_does_not_open_the_other_notification_paths(self):
        for op in ("set", "null", "silent"):
            with self.subTest(op=op), self.assertRaises(OverlayError):
                parse_overlay(doc({"atMs": 0, "op": op, "path": "notifications.mob", "value": 1}), B)

    def test_notify_delta_mimics_the_servers_own_notification(self):
        # What the server's POST /notifications/mob makes: notifications.mob.<uuid>, value.id, the server's position in
        # value.position, createdAt. The displays are tested against that shape.
        d = notify_delta(Notify("mob", "emergency", None, (41.5, -70.25)))
        self.assertEqual(d["context"], "vessels.self")
        v = d["updates"][0]["values"][0]
        uid = v["value"]["id"]
        self.assertRegex(uid, r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
        self.assertEqual(v["path"], f"notifications.mob.{uid}")
        self.assertEqual(v["value"]["state"], "emergency")
        self.assertEqual(v["value"]["method"], ["visual", "sound"])
        self.assertEqual(v["value"]["message"], "MAN OVERBOARD")
        self.assertEqual(v["value"]["position"], {"latitude": 41.5, "longitude": -70.25})
        self.assertIn("createdAt", v["value"])
        self.assertNotIn("data", v["value"])
        fire = notify_delta(Notify("fire", "emergency", None, None))["updates"][0]["values"][0]
        self.assertTrue(fire["path"].startswith("notifications.fire."))
        self.assertNotIn("position", fire["value"])   # a position only on a MOB
        self.assertEqual(fire["value"]["message"], "FIRE")

    def test_notify_normal_clears_the_same_notification(self):
        up = notify_delta(Notify("mob", "emergency", None, (41.5, -70.25)))["updates"][0]["values"][0]
        down = notify_delta(Notify("mob", "normal", None, None))["updates"][0]["values"][0]
        self.assertEqual(down["path"], up["path"])   # same id, so a display matches the clear to the raise
        self.assertEqual(down["value"], {"state": "normal", "id": up["value"]["id"]})
        other = notify_delta(Notify("fire", "emergency", None, None))["updates"][0]["values"][0]
        self.assertNotEqual(other["value"]["id"], up["value"]["id"])

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
