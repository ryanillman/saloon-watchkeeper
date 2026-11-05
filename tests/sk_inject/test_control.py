import asyncio
import socket
import tempfile
import unittest
from pathlib import Path

import _path  # noqa: F401
import control


def run(coro):
    return asyncio.run(coro)


class Control(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sock = Path(self.tmp.name) / "t.sock"

    def tearDown(self):
        self.tmp.cleanup()

    def test_socket_path_uses_xdg_runtime_dir(self):
        import os
        old = os.environ.get("XDG_RUNTIME_DIR")
        os.environ["XDG_RUNTIME_DIR"] = "/run/user/1234"
        try:
            self.assertEqual(control.socket_path(), Path("/run/user/1234/sk-inject.sock"))
        finally:
            if old is None:
                del os.environ["XDG_RUNTIME_DIR"]
            else:
                os.environ["XDG_RUNTIME_DIR"] = old

    def test_round_trip(self):
        async def main():
            async def handler(cmd):
                return {"ok": True, "echo": cmd}
            server = await control.serve(handler, self.sock)
            reply = await asyncio.to_thread(control.send, {"cmd": "status"}, self.sock)
            server.close()
            await server.wait_closed()
            return reply
        self.assertEqual(run(main()), {"ok": True, "echo": {"cmd": "status"}})

    def test_handler_exception_becomes_error_reply(self):
        async def main():
            async def handler(cmd):
                raise RuntimeError("boom")
            server = await control.serve(handler, self.sock)
            reply = await asyncio.to_thread(control.send, {"cmd": "x"}, self.sock)
            server.close()
            await server.wait_closed()
            return reply
        reply = run(main())
        self.assertFalse(reply["ok"])
        self.assertIn("boom", reply["error"])

    def test_send_with_no_daemon_raises_daemon_not_running(self):
        with self.assertRaises(control.DaemonNotRunning):
            control.send({"cmd": "status"}, self.sock)

    def test_stale_socket_file_is_replaced(self):
        dead = socket.socket(socket.AF_UNIX)
        dead.bind(str(self.sock))
        dead.close()                       # leaves the file with nobody listening
        self.assertTrue(self.sock.exists())

        async def main():
            async def handler(cmd):
                return {"ok": True}
            server = await control.serve(handler, self.sock)
            reply = await asyncio.to_thread(control.send, {"cmd": "status"}, self.sock)
            server.close()
            await server.wait_closed()
            return reply
        self.assertTrue(run(main())["ok"])

    def test_second_daemon_refused_while_first_is_alive(self):
        async def main():
            async def handler(cmd):
                return {"ok": True}
            first = await control.serve(handler, self.sock)
            try:
                with self.assertRaises(control.AlreadyRunning):
                    await control.serve(handler, self.sock)
            finally:
                first.close()
                await first.wait_closed()
        run(main())


if __name__ == "__main__":
    unittest.main()
