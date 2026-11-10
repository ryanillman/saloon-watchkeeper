#!/usr/bin/env python3
"""Asks the Signal K server for a write token for the boat daemon.

  inject.py access      request a token (approve it in the admin UI)

Playback now lives in boat.py (the long-running boat); drive it with tools/scenario.sh.
The token is kept in tools/sk-inject/.token (not committed).
"""
import json
import sys
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOKEN_FILE = HERE / ".token"
SERVER = "localhost:3000"


def request_access():
    body = json.dumps({
        "clientId": "saloon-watchkeeper-sk-inject",
        "description": "Saloon Watchkeeper test injector",
        "permissions": "readwrite",
    }).encode()
    req = urllib.request.Request(f"http://{SERVER}/signalk/v1/access/requests", body,
                                 {"Content-Type": "application/json"})
    resp = json.load(urllib.request.urlopen(req))
    href = resp.get("href")
    print("Access requested. Approve 'Saloon Watchkeeper test injector' in the Signal K admin UI")
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


if __name__ == "__main__":
    if sys.argv[1:] != ["access"]:
        sys.exit(__doc__)
    request_access()
