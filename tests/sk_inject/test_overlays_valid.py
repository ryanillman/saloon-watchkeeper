import json
import unittest

import _path  # noqa: F401
from boatmodel import load_baseline
from overlay import Alarm, Post, Put, parse_overlay

B = load_baseline(_path.SKDIR / "baseline.json")
DIR = _path.SKDIR / "overlays"
NAMES = {
    "alarm-shallow", "depth-sounder",
    "alarm-coolant", "alarm-fuel-low", "controller-dropout", "coolant-null", "glance-mixed",
    "key-off", "rpm-pickup-fails", "tanks", "three-bank", "two-engines", "victron-power",
    "warn-tank-low", "anchor-holding", "anchor-holding-anchoralarm", "navigation-underway",
}


def load(name):
    return json.loads((DIR / f"{name}.json").read_text())


class OverlaysValid(unittest.TestCase):
    def test_names_are_the_known_overlays(self):
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
                "warn-tank-low": ("tanks.freshWater.0.currentLevel", "warn"),
                "alarm-shallow": ("environment.depth.belowTransducer", "alarm")}
        for name, (path, state) in want.items():
            ov = parse_overlay(load(name), B)
            acts = ov.step(10 ** 9)
            self.assertIn(Alarm(path, state, None), acts, name)

    def test_anchor_overlays_let_the_plugin_do_the_alarming(self):
        # the anchor plugins raise the alarm from the position; the overlay only sets up and moves the boat
        import math
        for name, drop in (("anchor-holding", "/plugins/hoekens-anchor-alarm/dropAnchor"),
                           ("anchor-holding-anchoralarm", "/plugins/anchoralarm/dropAnchor")):
            with self.subTest(overlay=name):
                ov = parse_overlay(load(name), B)
                # an anchored boat has its engines off: Hoeken (enableEngineCheck) drops its watch otherwise
                self.assertEqual(ov.profile, "anchor")
                acts = ov.step(10 ** 9)
                self.assertTrue(any(isinstance(a, Post) and a.path == drop for a in acts))
                # only the commanded plugin may watch: the other one's anchor is cleared first
                other = "/plugins/anchoralarm/raiseAnchor" if "hoeken" in drop else "/plugins/hoekens-anchor-alarm/raiseAnchor"
                self.assertIn(Post(other, None), acts)
                self.assertFalse(any(isinstance(a, Alarm) for a in acts))
                evs = [(e["atMs"], e) for e in load(name)["after"] if e["op"] == "move"]
                dist = lambda e: math.hypot(e["northM"], e["eastM"])        # noqa: E731
                before = [dist(e) for t, e in evs if t < 40000]
                self.assertTrue(before and all(10 < d < 45 for d in before), "swings stay inside 45 m")
                at40 = [dist(e) for t, e in evs if t == 40000]
                self.assertTrue(at40 and at40[0] > 45, "drifts past the limit at 40 s")
                at70 = [dist(e) for t, e in evs if t == 70000]
                self.assertTrue(at70 and at70[0] < 45, "back inside at 70 s")

    def test_anchoralarm_overlay_names_the_source_of_its_radius_put(self):
        # both plugins publish navigation.anchor.maxRadius; the server refuses a PUT that names no source
        puts = [a for a in parse_overlay(load("anchor-holding-anchoralarm"), B).step(10 ** 9)
                if isinstance(a, Put)]
        self.assertEqual([(p.path, p.source) for p in puts],
                         [("navigation.anchor.maxRadius", "anchoralarm")])

    def test_three_bank_enables_its_group(self):
        self.assertEqual(parse_overlay(load("three-bank"), B).with_groups, {"three-bank"})
        self.assertEqual(parse_overlay(load("glance-mixed"), B).profile, "anchor")


if __name__ == "__main__":
    unittest.main()


class NavigationUnderway(unittest.TestCase):
    def test_runs_with_a_course_and_an_autopilot(self):
        ov = parse_overlay(load("navigation-underway"), B)
        self.assertEqual(ov.with_groups, {"course", "autopilot"})
        self.assertEqual(ov.profile, None)   # underway: the routine boat

    def test_phases_take_things_away_and_give_them_back(self):
        doc = load("navigation-underway")
        steps = [(e["atMs"], e["op"], e.get("path")) for e in doc["after"]]
        dtk = "navigation.course.calcValues.bearingTrackTrue"
        self.assertIn((30000, "null", dtk), steps)
        self.assertIn((50000, "clear", dtk), steps)
        self.assertIn((70000, "set", "steering.autopilot.state"), steps)   # standby
        self.assertIn((90000, "clear", "steering.autopilot.state"), steps)
        for p in ("environment.wind.speedTrue", "environment.wind.angleTrueWater"):   # the display computes true wind
            self.assertIn((110000, "null", p), steps)
            self.assertIn((130000, "clear", p), steps)
        self.assertIn((150000, "silent", "navigation.headingMagnetic"), steps)   # the heading fails until the end

    def test_it_never_writes_notifications(self):
        for ev in load("navigation-underway")["after"]:
            self.assertFalse(str(ev.get("path", "")).startswith("notifications."))
