import asyncio
import json
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

import _path  # noqa: F401
import zones

ROUTES = {}


class H(BaseHTTPRequestHandler):
    def do_GET(self):
        body = ROUTES.get(self.path)
        if body is None:
            self.send_response(404)
            self.end_headers()
            return
        data = json.dumps(body() if callable(body) else body).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *a):
        pass


class ZonesIO(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.srv = HTTPServer(("127.0.0.1", 0), H)
        cls.server = f"127.0.0.1:{cls.srv.server_port}"
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        cls.srv.server_close()

    def setUp(self):
        ROUTES.clear()

    def test_read_zones_returns_list(self):
        z = [{"lower": 0, "upper": 1, "state": "nominal"}]
        ROUTES["/signalk/v1/api/vessels/self/tanks/fuel/0/currentLevel/meta"] = {"units": "ratio", "zones": z}
        self.assertEqual(zones.read_zones(self.server, "tanks.fuel.0.currentLevel"), z)

    def test_read_zones_none_when_meta_has_no_zones(self):
        ROUTES["/signalk/v1/api/vessels/self/propulsion/port/temperature/meta"] = {"units": "K"}
        self.assertIsNone(zones.read_zones(self.server, "propulsion.port.temperature"))

    def test_read_zones_none_on_404_and_empty_list(self):
        self.assertIsNone(zones.read_zones(self.server, "no.such.path"))
        ROUTES["/signalk/v1/api/vessels/self/a/b/meta"] = {"zones": []}
        self.assertIsNone(zones.read_zones(self.server, "a.b"))

    def test_read_zones_raises_when_server_unreachable(self):
        with self.assertRaises(OSError):
            zones.read_zones("127.0.0.1:1", "a.b")

    def test_meta_delta_shape(self):
        d = zones.meta_delta("a.b", {"zones": []})
        self.assertEqual(d["context"], "vessels.self")
        self.assertEqual(d["updates"][0]["meta"], [{"path": "a.b", "value": {"zones": []}}])
        self.assertNotIn("values", d["updates"][0])

    def test_raise_check_true_when_state_appears(self):
        calls = []

        def note():
            calls.append(1)
            return {"value": {"state": "alarm" if len(calls) >= 3 else "normal"}}
        ROUTES["/signalk/v1/api/vessels/self/notifications/propulsion/port/temperature"] = note
        ok = asyncio.run(zones.raise_check(self.server, "propulsion.port.temperature", "alarm", 5, poll_s=0.01))
        self.assertTrue(ok)

    def test_raise_check_false_on_timeout(self):
        ROUTES["/signalk/v1/api/vessels/self/notifications/x/y"] = {"value": {"state": "normal"}}
        self.assertFalse(asyncio.run(zones.raise_check(self.server, "x.y", "alarm", 0.1, poll_s=0.01)))

    def test_record_created_is_idempotent(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "zones-created.json"
            zones.record_created("a.b", f)
            zones.record_created("a.b", f)
            zones.record_created("c.d", f)
            self.assertEqual(json.loads(f.read_text()), ["a.b", "c.d"])
            self.assertEqual(zones.created_paths(f), ["a.b", "c.d"])


if __name__ == "__main__":
    unittest.main()
