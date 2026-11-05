#!/usr/bin/env python3
"""Client for the boat daemon (called by tools/scenario.sh).

  ctl.py status | zones | down
  ctl.py overlay NAME
  ctl.py alarm PATH STATE [HOLD_SECONDS]
  ctl.py clear PATH
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # python -I leaves the script dir off the path
import control  # noqa: E402


def build(argv):
    c = argv[0] if argv else ""
    if c in ("status", "zones", "down") and len(argv) == 1:
        return {"cmd": c}
    if c == "overlay" and len(argv) == 2:
        return {"cmd": c, "name": argv[1]}
    if c == "clear" and len(argv) == 2:
        return {"cmd": c, "path": argv[1]}
    if c == "alarm" and len(argv) in (3, 4):
        cmd = {"cmd": c, "path": argv[1], "state": argv[2]}
        if len(argv) == 4:
            cmd["holdMs"] = float(argv[3]) * 1000
        return cmd
    sys.exit(__doc__)


def show(cmd, r):
    c = cmd["cmd"]
    if c == "status":
        print(f"boat up {r['uptimeS']} s, profile {r['profile']}, "
              f"server {r['server']} {'connected' if r['connected'] else 'UNREACHABLE'}")
        for o in r["overlays"]:
            print(f"  overlay {o['name']}: {o['elapsedMs'] / 1000:.0f}/{o['durationMs'] / 1000:.0f} s")
        if r["overrides"]:
            print("  overriding:", ", ".join(r["overrides"]))
        if r["silent"]:
            print("  silent:", ", ".join(r["silent"]))
    elif c == "overlay":
        print(f"playing for {r['durationMs'] / 1000:g} s: {' '.join(r['description'].split())}")
        print("(events go to the daemon log: tools/scenario.sh log)")
    elif c == "alarm":
        for m in r["messages"]:
            print(m)
        if not r["raised"]:
            sys.exit(1)
    elif c == "zones":
        print("\n".join(r["created"]) or "no zones created by sk-inject")


def main():
    cmd = build(sys.argv[1:])
    try:
        r = control.send(cmd)
    except control.DaemonNotRunning as e:
        sys.exit(str(e))
    if not r.get("ok"):
        sys.exit(r.get("error", "failed"))
    show(cmd, r)


if __name__ == "__main__":
    main()
