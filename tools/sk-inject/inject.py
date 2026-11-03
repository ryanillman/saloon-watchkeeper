#!/usr/bin/env python3
"""Pushes scripted data into a Signal K server for testing the displays.

  inject.py access                      ask the server for a write token (approve it in the admin UI)
  inject.py scenarios/two-engines.json  play a scenario

The token is kept in tools/sk-inject/.token (not committed).
"""
import asyncio
import json
import math
import sys
import time
import urllib.request
from pathlib import Path

import websockets

HERE = Path(__file__).resolve().parent
TOKEN_FILE = HERE / ".token"
SERVER = "localhost:3000"


def request_access():
    body = json.dumps({
        "clientId": "boat-instruments-sk-inject",
        "description": "boat-instruments test injector",
        "permissions": "readwrite",
    }).encode()
    req = urllib.request.Request(f"http://{SERVER}/signalk/v1/access/requests", body,
                                 {"Content-Type": "application/json"})
    resp = json.load(urllib.request.urlopen(req))
    href = resp.get("href")
    print("Access requested. Approve 'boat-instruments test injector' in the Signal K admin UI")
    print(f"(http://{SERVER}/admin → Security → Access Requests), with read/write permission.")
    while True:
        time.sleep(2)
        state = json.load(urllib.request.urlopen(f"http://{SERVER}{href}"))
        if state.get("state") == "COMPLETED":
            access = state.get("accessRequest", {})
            if access.get("permission") != "APPROVED":
                sys.exit(f"Request was not approved: {access.get('permission')}")
            TOKEN_FILE.write_text(access["token"])
            TOKEN_FILE.chmod(0o600)
            print(f"Approved. Token saved to {TOKEN_FILE}")
            return


def value_at(spec, t, duration):
    """A scenario value: a number, null, or {"from": a, "to": b} ramped over the duration."""
    if isinstance(spec, dict) and "from" in spec:
        frac = min(1.0, t / duration) if duration else 1.0
        wobble = spec.get("wobble", 0) * math.sin(t / 1500.0)
        return spec["from"] + (spec["to"] - spec["from"]) * frac + wobble
    return spec


def delta(values):
    return {
        "context": "vessels.self",
        "updates": [{
            "source": {"label": "sk-inject"},
            "values": [{"path": p, "value": v} for p, v in values.items()],
        }],
    }


async def play(path):
    sc = json.loads(Path(path).read_text())
    if not TOKEN_FILE.exists():
        sys.exit("No token. Run: inject.py access")
    token = TOKEN_FILE.read_text().strip()
    duration = sc.get("durationMs", 60000)
    interval = sc.get("intervalMs", 1000)
    events = sorted(sc.get("after", []), key=lambda e: e["atMs"])
    values = dict(sc.get("values", {}))
    async with websockets.connect(f"ws://{SERVER}/signalk/v1/stream?subscribe=none",
                                  additional_headers={"Authorization": f"Bearer {token}"}) as ws:
        start = time.monotonic()
        while True:
            t = (time.monotonic() - start) * 1000
            while events and events[0]["atMs"] <= t:
                e = events.pop(0)
                print(f"{t/1000:5.1f}s  {e.get('note', 'event')}")
                values.update(e["values"])
            await ws.send(json.dumps(delta({p: value_at(s, t, duration) for p, s in values.items()})))
            if t >= duration:
                break
            await asyncio.sleep(interval / 1000)
    print("done")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    if sys.argv[1] == "access":
        request_access()
    else:
        asyncio.run(play(sys.argv[1]))
