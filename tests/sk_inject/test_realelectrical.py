import json
import unittest

import _path  # noqa: F401
import mkreal
from boatmodel import build_delta, load_baseline

B = load_baseline(_path.SKDIR / "baseline.json")
REAL = json.loads((_path.SKDIR / "real-electrical.json").read_text())
CAPTURE = _path.SKDIR.parents[1] / "tests" / "fixtures" / "electrical" / "capture.candump"
SYNTHETIC = ["electrical.batteries.279.voltage", "electrical.solar.288.current", "electrical.chargers.276.current",
             "electrical.inverters.275.inverterMode", "electrical.alternators.0.voltage"]


def leaves():
    """(path, source address, value) for every value of the fixture."""
    for u in REAL["updates"]:
        for v in u["values"]:
            yield v["path"], u["source"]["src"], v["value"]


class RealProfile(unittest.TestCase):
    def test_real_profile_sends_nothing_for_the_synthetic_electrical_boat(self):
        vals = B.values(0.0, "real", B.default_on("real"), {})
        for p in SYNTHETIC:
            with self.subTest(path=p):
                # Silence, not null: a path that exists with a null value is a failed sender to a display, and it
                # would show as a failed inverter, engine and solar node.
                self.assertNotIn(p, vals)
        self.assertFalse([p for p in vals if p.startswith("electrical.")])
        self.assertIsNotNone(vals["propulsion.port.revolutions"])   # the rest of the boat is untouched

    def test_other_profiles_are_unchanged(self):
        for profile in ("underway", "anchor"):
            self.assertIsNotNone(B.values(0.0, profile, B.default_on(profile), {})["electrical.batteries.279.voltage"])
            self.assertEqual(B.real_updates(profile), [])

    def test_real_profile_sends_the_capture(self):
        self.assertEqual(B.real_updates("real"), REAL["updates"])
        self.assertTrue(B.real_updates("real"))

    def test_an_overlay_can_still_set_a_silenced_path(self):
        vals = B.values(0.0, "real", B.default_on("real"), {"electrical.batteries.279.voltage": 12.0})
        self.assertEqual(vals["electrical.batteries.279.voltage"], 12.0)


class Fixture(unittest.TestCase):
    def test_every_update_names_its_device(self):
        for u in REAL["updates"]:
            s = u["source"]
            with self.subTest(source=s):
                self.assertEqual((s["label"], s["type"]), ("can0", "NMEA2000"))
                self.assertIsInstance(s["src"], str)
                self.assertIsInstance(s["pgn"], int)

    def test_only_electrical_paths_and_plain_values(self):
        for path, _, value in leaves():
            self.assertTrue(path.startswith("electrical."), path)
            if value is None:   # what the server showed for a time remaining beyond any estimate
                self.assertTrue(path.endswith("capacity.timeRemaining"), path)
                continue
            self.assertIsInstance(value, (int, float, str))
            self.assertNotIsInstance(value, bool)

    def test_instances_collide_across_devices_as_on_the_real_bus(self):
        sources = {src for path, src, _ in leaves() if path == "electrical.batteries.0.voltage"}
        self.assertGreaterEqual(len(sources), 3)   # shunt, GX mirror is 239, but both solar controllers and the charger report 0

    def test_a_panel_input_is_published_as_a_battery_instance(self):
        pv = [v for path, src, v in leaves() if path == "electrical.batteries.1.voltage" and src in ("36", "37")]
        self.assertTrue(pv)
        self.assertTrue(all(v < 8 for v in pv))   # a dark panel: nothing a battery could read

    def test_converter_states_are_names(self):
        states = {v for path, _, v in leaves() if path.endswith(".operatingState")}
        self.assertTrue(states <= {"off", "bulk", "absorption", "float", "storage", "equalize", "fault"}, states)
        self.assertGreaterEqual(len(states), 2)

    def test_devices_carry_their_identity(self):
        seen = {}
        for u in REAL["updates"]:
            seen.setdefault(u["source"]["src"], []).append(u["source"])
        for src, sources in seen.items():
            for s in sources:
                with self.subTest(src=src):
                    self.assertRegex(s["canName"], r"^[0-9a-f]{16}$")
                    self.assertTrue(s["modelId"])


class Delta(unittest.TestCase):
    def test_baseline_first_then_each_device_as_its_own_update(self):
        d = build_delta({"a.b": 1.0}, REAL["updates"])
        self.assertEqual(d["context"], "vessels.self")
        self.assertEqual(d["updates"][0], {"source": {"label": "sk-inject"}, "values": [{"path": "a.b", "value": 1.0}]})
        self.assertEqual(d["updates"][1:], REAL["updates"])

    def test_without_real_data_it_is_the_old_single_update(self):
        self.assertEqual(len(build_delta({"a.b": 1.0}, [])["updates"]), 1)


class Generator(unittest.TestCase):
    def test_the_fixture_is_what_the_generator_makes_from_the_capture(self):
        devices = json.loads((_path.SKDIR / "real-electrical-devices.json").read_text())
        self.assertEqual(mkreal.build(CAPTURE.read_text(), devices), REAL)

    def test_decodes_a_dc_status_frame(self):
        # 127508 from the capture: instance 0, 14.38 V, 0 A
        out = mkreal.decode_frames(["(1.0) can0 19F21425#009E050000FFFF2E"])
        self.assertEqual(out[("37", 127508)]["electrical.batteries.0.voltage"], 14.38)
        self.assertEqual(out[("37", 127508)]["electrical.batteries.0.current"], 0.0)

    def test_time_remaining_beyond_any_estimate_is_null_as_the_server_showed_it(self):
        # 127506 is fast-packet: SID 0x20, instance 0, DC type 0, SoC 100 %, SoH n/a, 14399 min (0x383F, the 240 h cap)
        out = mkreal.decode_frames(["(1.0) can0 19F212E0#000B20000064FF3F", "(1.1) can0 19F212E0#0138FFFFFFFFFFFF"])
        self.assertIsNone(out[("224", 127506)]["electrical.batteries.0.capacity.timeRemaining"])
        self.assertEqual(out[("224", 127506)]["electrical.batteries.0.capacity.stateOfCharge"], 1.0)

    def test_not_available_values_are_dropped(self):
        out = mkreal.decode_frames(["(1.0) can0 19F21425#017205FF7FFFFF4C"])
        self.assertEqual(out[("37", 127508)], {"electrical.batteries.1.voltage": 13.94})   # current 0x7FFF: n/a


if __name__ == "__main__":
    unittest.main()
