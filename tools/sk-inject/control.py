"""Control channel between the boat daemon and tools/scenario.sh.

One JSON request per connection, one JSON reply, newline-terminated, over a Unix socket.
"""
import asyncio
import json
import os
import socket
from pathlib import Path


class DaemonNotRunning(Exception):
    pass


class AlreadyRunning(Exception):
    pass


def socket_path() -> Path:
    base = os.environ.get("XDG_RUNTIME_DIR")
    if not base:
        base = f"/tmp/sk-inject-{os.getuid()}"
        Path(base).mkdir(mode=0o700, exist_ok=True)
    return Path(base) / "sk-inject.sock"


def send(cmd: dict, path=None, timeout=60.0) -> dict:
    """Send one command to the daemon and return its reply."""
    path = Path(path or socket_path())
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(timeout)
    try:
        s.connect(str(path))
    except (FileNotFoundError, ConnectionRefusedError):
        s.close()
        raise DaemonNotRunning("boat daemon is not running (start it with: tools/scenario.sh up)")
    try:
        s.sendall(json.dumps(cmd).encode() + b"\n")
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
        return json.loads(buf)
    finally:
        s.close()


async def serve(handler, path=None):
    """Listen on the socket; handler(cmd: dict) -> dict (a coroutine). Replaces a stale socket file."""
    path = Path(path or socket_path())
    if path.exists():
        try:
            await asyncio.to_thread(send, {"cmd": "status"}, path, 2.0)
        except DaemonNotRunning:
            path.unlink()          # nobody answers: stale file from a crashed daemon
        except OSError:
            path.unlink()
        else:
            raise AlreadyRunning(f"a boat daemon is already running on {path}")

    async def on_conn(reader, writer):
        try:
            line = await reader.readline()
            try:
                reply = await handler(json.loads(line))
            except Exception as e:                      # reported to the client, daemon keeps going
                reply = {"ok": False, "error": f"{type(e).__name__}: {e}"}
            writer.write(json.dumps(reply).encode() + b"\n")
            await writer.drain()
        finally:
            writer.close()

    server = await asyncio.start_unix_server(on_conn, str(path))
    os.chmod(path, 0o600)
    return server
