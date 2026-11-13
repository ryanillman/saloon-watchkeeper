#!/usr/bin/env python3
"""The boat: sends routine values for every Signal K path the displays cover, continuously.

  boat.py [--profile underway|anchor] [--with three-bank] [--server localhost:3000]

Overlays (faults, dropouts, alarms) are commands from tools/scenario.sh over a Unix socket.
An alarm is never written as a notification: the daemon makes sure the path has zones on the
server (creating a default set if there are none) and drives the value across a zone boundary,
so Signal K raises, escalates and clears the notification itself.
"""
import argparse
import asyncio
import json
import signal
import sys
import time
from pathlib import Path

import websockets

sys.path.insert(0, str(Path(__file__).resolve().parent))   # python -I leaves the script dir off the path
import control  # noqa: E402
import skhttp  # noqa: E402
import zones  # noqa: E402
from boatmodel import build_delta, load_baseline  # noqa: E402
from boatstate import MANUAL, BoatState  # noqa: E402
from overlay import Alarm, Notify, OverlayError, Post, notify_delta, parse_overlay  # noqa: E402

HERE = Path(__file__).resolve().parent
TOKEN_FILE = HERE / ".token"
OVERLAYS = HERE / "overlays"
ALARM_RAMP_MS = 5000
RAISE_WAIT_S = 8.0


class Boat:
    def __init__(self, args, token):
        self.server = args.server
        self.token = token
        self.interval = args.interval
        self.baseline = load_baseline(HERE / "baseline.json")
        self.state = BoatState(self.baseline, args.profile, args.with_groups)
        self.t0 = time.monotonic()
        self.ws = None
        self.stop = asyncio.Event()
        self._tasks = set()

    def now(self):
        return (time.monotonic() - self.t0) * 1000.0

    def log(self, msg):
        print(f"{time.strftime('%H:%M:%S')}  {msg}", flush=True)

    # -- server connection ------------------------------------------------------------
    async def connection(self):
        url = f"ws://{self.server}/signalk/v1/stream?subscribe=none"
        headers = {"Authorization": f"Bearer {self.token}"}
        backoff, down_logged = 1.0, False
        while not self.stop.is_set():
            try:
                async with websockets.connect(url, additional_headers=headers) as ws:
                    self.ws = ws
                    self.log(f"connected to {self.server}")
                    backoff, down_logged = 1.0, False
                    await ws.wait_closed()
            except (OSError, websockets.WebSocketException) as e:
                if not down_logged:
                    self.log(f"server unreachable ({type(e).__name__}); retrying, boat keeps its state")
                    down_logged = True
            if self.ws is not None and not down_logged:
                self.log("connection lost; retrying")
                down_logged = True
            self.ws = None
            try:
                await asyncio.wait_for(self.stop.wait(), timeout=backoff)
            except asyncio.TimeoutError:
                pass
            backoff = min(backoff * 2, 15.0)

    async def send(self, delta):
        if self.ws is None:
            return False
        try:
            await self.ws.send(json.dumps(delta))
            return True
        except websockets.ConnectionClosed:
            self.ws = None
            return False

    # -- the clock --------------------------------------------------------------------
    async def ticker(self):
        while not self.stop.is_set():
            values, alarms, msgs = self.state.tick(self.now())
            for m in msgs:
                self.log(m)
            for owner, a in alarms:
                self.spawn(self.alarm(owner, a) if isinstance(a, Alarm) else self.notify(a) if isinstance(a, Notify) else self.request(a))
            await self.send(build_delta(values, self.state.baseline.real_updates(self.state.profile)))
            try:
                await asyncio.wait_for(self.stop.wait(), timeout=self.interval)
            except asyncio.TimeoutError:
                pass

    def spawn(self, coro):
        t = asyncio.create_task(coro)
        self._tasks.add(t)
        t.add_done_callback(self._tasks.discard)

    # -- requests: what a display's buttons do (drop or raise an anchor, set its radius) ---
    async def request(self, a):
        """Send a POST or PUT from an overlay. Best effort: a failure is logged, the overlay carries on."""
        if isinstance(a, Post):
            what, call = f"POST {a.path}", (skhttp.post_json, a.path, a.body)
        else:
            what, call = f"PUT {a.path}={a.value}", (skhttp.put_value, a.path, a.value, a.source)
        fn, *args = call
        try:
            status, body = await asyncio.to_thread(fn, self.server, self.token, *args)
        except OSError as e:
            self.log(f"{what}: failed ({e})")
            return
        note = "" if 200 <= status < 300 else f"  {' '.join(body.split())[:300]}"
        self.log(f"{what}: HTTP {status}{note}")

    async def notify(self, n: Notify):
        """An emergency notification (mob, fire, ...): no zone to cross, so it goes out as a delta, as a plugin would send it."""
        sent = await self.send(notify_delta(n))
        self.log(f"notifications.{n.nature} {n.state}: {'sent' if sent else 'NOT sent (the server connection is down)'}")

    # -- alarms: let the server do the alarming ---------------------------------------
    async def alarm(self, owner, a: Alarm):
        """Ensure zones, drive the value into the requested zone, then confirm the server raised it."""
        out = []

        def say(m):
            out.append(m)
            self.log(f"{a.path}: {m}")

        try:
            zs = await asyncio.to_thread(zones.read_zones, self.server, a.path)
        except OSError as e:
            say(f"cannot read zones ({e}); alarm not raised")
            return False, out
        if zs is None:
            meta = zones.default_zones(a.path)
            if meta is None:
                say("no zones on the server and no default zone set for this path; alarm not raised")
                return False, out
            if not await self.send(zones.meta_delta(a.path, meta)):
                say("no zones, and the server connection is down so none can be created; alarm not raised")
                return False, out
            zones.record_created(a.path)
            zs = meta["zones"]
            say("had no zones; created a default set")
        try:
            target = zones.pick_target(zs, a.state)
        except zones.NoSuchState as e:
            say(f"has zones but none with state '{a.state}' (has: {', '.join(e.states)}); left unchanged")
            return False, out
        if not self.state.drive_to(owner, a.path, target, self.now(), ALARM_RAMP_MS, a.hold_ms):
            say("its overlay ended before the alarm could start; value left alone")
            return False, out
        say(f"driving to {target:g} for a '{a.state}' zone")
        raised = await zones.raise_check(self.server, a.path, a.state, ALARM_RAMP_MS / 1000 + RAISE_WAIT_S)
        if raised:
            say(f"server raised '{a.state}'")
        else:
            say(f"WARNING: server did not raise '{a.state}' (zones not applied, or the path is silent)")
        return raised, out

    # -- control ----------------------------------------------------------------------
    async def handle(self, cmd):
        c = cmd.get("cmd")
        now = self.now()
        if c == "status":
            st = self.state.status(now)
            st.update(ok=True, server=self.server, connected=self.ws is not None,
                      uptimeS=round(now / 1000))
            return st
        if c == "overlay":
            name = cmd["name"]
            f = OVERLAYS / f"{name}.json"
            if not f.is_file():
                return {"ok": False, "error": f"no overlay named {name}"}
            try:
                ov = parse_overlay(json.loads(f.read_text()), self.baseline)
            except (OverlayError, ValueError) as e:
                return {"ok": False, "error": f"{name}: {e}"}
            self.state.start_overlay(name, ov, now)
            self.log(f"overlay {name} started ({ov.duration_ms / 1000:g} s)")
            return {"ok": True, "durationMs": ov.duration_ms, "description": ov.description}
        if c == "alarm":
            hold = cmd.get("holdMs")
            raised, msgs = await self.alarm(MANUAL, Alarm(cmd["path"], cmd["state"], hold))
            return {"ok": True, "raised": raised, "messages": msgs}
        if c == "clear":
            self.state.clear(cmd["path"])
            self.log(f"{cmd['path']}: cleared")
            return {"ok": True}
        if c == "zones":
            return {"ok": True, "created": zones.created_paths()}
        if c == "down":
            self.log("stopping")
            asyncio.get_running_loop().call_later(0.2, self.stop.set)
            return {"ok": True}
        return {"ok": False, "error": f"unknown command {c!r}"}

    async def run(self):
        server = await control.serve(self.handle)
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, self.stop.set)
        self.log(f"boat up: profile {self.state.profile}, server {self.server}")
        conn = asyncio.create_task(self.connection())
        try:
            await self.ticker()
        finally:
            conn.cancel()
            server.close()
            await server.wait_closed()
            control.socket_path().unlink(missing_ok=True)
            self.log("boat down")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", default="underway")
    ap.add_argument("--with", dest="with_groups", action="append", default=[])
    ap.add_argument("--server", default="localhost:3000")
    ap.add_argument("--interval", type=float, default=1.0)
    args = ap.parse_args()
    if not TOKEN_FILE.exists():
        sys.exit("No token. Run: tools/scenario.sh access")
    baseline = load_baseline(HERE / "baseline.json")
    if args.profile not in baseline.profiles:
        sys.exit(f"unknown profile {args.profile!r} (have: {', '.join(baseline.profiles)})")
    for g in args.with_groups:
        if g not in baseline.groups():
            sys.exit(f"unknown group {g!r} (have: {', '.join(sorted(baseline.groups()))})")
    try:
        asyncio.run(Boat(args, TOKEN_FILE.read_text().strip()).run())
    except control.AlreadyRunning as e:
        sys.exit(str(e))


if __name__ == "__main__":
    main()
