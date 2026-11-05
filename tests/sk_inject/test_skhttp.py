import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer

import _path  # noqa: F401
import skhttp

SEEN = []
STATUS = {"code": 200}


class H(BaseHTTPRequestHandler):
    def _do(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n).decode()
        SEEN.append((self.command, self.path, self.headers.get("Authorization"),
                     self.headers.get("Content-Type"), body))
        self.send_response(STATUS["code"])
        self.end_headers()
        self.wfile.write(b'{"state":"COMPLETED"}')

    do_POST = _do
    do_PUT = _do

    def log_message(self, *a):
        pass


class SkHttp(unittest.TestCase):
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
        SEEN.clear()
        STATUS["code"] = 200

    def test_post_sends_json_body_with_bearer_token(self):
        status, _ = skhttp.post_json(self.server, "tok", "/plugins/anchoralarm/dropAnchor", {"radius": 45})
        self.assertEqual(status, 200)
        method, path, auth, ctype, body = SEEN[0]
        self.assertEqual((method, path), ("POST", "/plugins/anchoralarm/dropAnchor"))
        self.assertEqual(auth, "Bearer tok")
        self.assertEqual(ctype, "application/json")
        self.assertEqual(json.loads(body), {"radius": 45})

    def test_post_without_a_body_sends_an_empty_object(self):
        skhttp.post_json(self.server, "tok", "/plugins/anchoralarm/raiseAnchor", None)
        self.assertEqual(json.loads(SEEN[0][4]), {})

    def test_put_value_uses_the_signalk_path_and_value_wrapper(self):
        status, _ = skhttp.put_value(self.server, "tok", "navigation.anchor.maxRadius", 45)
        self.assertEqual(status, 200)
        method, path, auth, _, body = SEEN[0]
        self.assertEqual((method, path), ("PUT", "/signalk/v1/api/vessels/self/navigation/anchor/maxRadius"))
        self.assertEqual(auth, "Bearer tok")
        self.assertEqual(json.loads(body), {"value": 45})

    def test_put_can_name_the_source(self):
        # with two plugins publishing one path the server refuses a PUT that names no source
        skhttp.put_value(self.server, "tok", "navigation.anchor.maxRadius", 45, source="anchoralarm")
        self.assertEqual(json.loads(SEEN[0][4]), {"value": 45, "source": "anchoralarm"})

    def test_put_without_a_source_sends_only_the_value(self):
        skhttp.put_value(self.server, "tok", "navigation.anchor.maxRadius", 45)
        self.assertEqual(json.loads(SEEN[0][4]), {"value": 45})

    def test_error_status_is_returned_not_raised(self):
        STATUS["code"] = 404
        status, _ = skhttp.post_json(self.server, "tok", "/plugins/nope/dropAnchor", {})
        self.assertEqual(status, 404)

    def test_unreachable_server_raises_oserror(self):
        with self.assertRaises(OSError):
            skhttp.put_value("127.0.0.1:1", "tok", "a.b", 1)


if __name__ == "__main__":
    unittest.main()
