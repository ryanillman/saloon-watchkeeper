#!/bin/sh
# Drive the long-running boat that feeds Signal K (tools/sk-inject/boat.py).
#   tools/scenario.sh up [--profile anchor] [--with three-bank]   start the boat (routine values for every path)
#   tools/scenario.sh down | status | zones | log
#   tools/scenario.sh                  list the overlays
#   tools/scenario.sh fuel             play the overlay alarm-fuel-low (a unique prefix is enough)
#   tools/scenario.sh --dry-run fuel   print the overlay file, do not play
#   tools/scenario.sh alarm PATH STATE [HOLD_SECONDS]   raise one alarm by hand, e.g.
#                                      alarm propulsion.port.temperature alarm
#   tools/scenario.sh clear PATH       send a path back to its routine value
#   tools/scenario.sh access           request a write token (see docs/testing.md)
# Alarms are raised by Signal K itself: the boat makes sure the path has zones, then drives the
# value across a zone boundary. Zones it creates are listed by `zones` and left in place.
set -eu
cd "$(dirname "$0")/.."

# The firmware shell puts the ESP-IDF virtualenv first on PATH. That Python
# does not have the websockets package the boat needs. Use the system one.
py=/usr/bin/python3
sk=tools/sk-inject
dir=$sk/overlays
log="${XDG_RUNTIME_DIR:-/tmp}/sk-inject.log"

ctl() { "$py" -I $sk/ctl.py "$@"; }

need_websockets() {
  if ! "$py" -I -c 'import websockets' >/dev/null 2>&1; then
    echo "Python is missing the websockets package the boat needs." >&2
    echo "Install it with: sudo apt install python3-websockets" >&2
    exit 1
  fi
}

dry=0
if [ "${1:-}" = "--dry-run" ]; then
  dry=1
  shift
fi

# Commands. A reserved word with the right number of arguments is a command; otherwise it is an
# overlay name or prefix (alarm-coolant is reachable as `alarm-c`, and `alarm` alone is ambiguous).
case "${1:-}" in
  access)
    exec "$py" -I $sk/inject.py access ;;
  up)
    shift
    if ctl status >/dev/null 2>&1; then
      echo "boat already running"; ctl status; exit 0
    fi
    [ -f $sk/.token ] || { echo "No token. Run: tools/scenario.sh access" >&2; exit 1; }
    need_websockets
    PYTHONUNBUFFERED=1 nohup "$py" -I $sk/boat.py "$@" > "$log" 2>&1 &
    i=0
    while [ $i -lt 50 ]; do
      if ctl status >/dev/null 2>&1; then ctl status; echo "log: $log"; exit 0; fi
      if ! kill -0 $! 2>/dev/null; then cat "$log" >&2; exit 1; fi
      i=$((i + 1)); sleep 0.1
    done
    echo "boat did not come up; see $log" >&2; exit 1 ;;
  down|status|zones)
    [ $# -eq 1 ] && exec "$py" -I $sk/ctl.py "$1" ;;
  log)
    exec tail -n 40 -f "$log" ;;
  alarm)
    if [ $# -ge 3 ] && [ $# -le 4 ]; then exec "$py" -I $sk/ctl.py "$@"; fi ;;
  clear)
    if [ $# -eq 2 ]; then exec "$py" -I $sk/ctl.py "$@"; fi ;;
esac

# list, or print the one stem that matches $1. A failed match writes the choices to stderr.
pick() {
  "$py" -I - "$dir" "$1" << 'PY'
import json, sys
from pathlib import Path

d = Path(sys.argv[1])
cmd = sys.argv[2]
files = sorted(d.glob("*.json"))
if cmd == "list":
    rows = []
    for p in files:
        sc = json.loads(p.read_text())
        ms = int(sc.get("durationMs") or 0)
        desc = " ".join(str(sc.get("description", "")).split())
        rows.append((p.stem, f"{ms // 1000}s" if ms else "", desc))
    w = max((len(n) for n, _, _ in rows), default=0)
    for name, secs, desc in rows:
        print(f"{name:<{w}}  {secs:>4}  {desc}")
    sys.exit(0)

q = cmd.lower()
stems = [p.stem for p in files]
exact = [s for s in stems if s.lower() == q]
pref = [s for s in stems if s.lower().startswith(q)]
sub = [s for s in stems if q in s.lower()]
if exact:
    chosen = exact
elif len(pref) == 1:
    chosen = pref
elif len(sub) == 1:
    chosen = sub
else:
    chosen = []
if len(chosen) == 1:
    print(chosen[0])
    sys.exit(0)
cands = pref or sub
if not cands:
    sys.stderr.write(f"no overlay matches '{cmd}'\n")
else:
    sys.stderr.write(f"'{cmd}' matches more than one overlay:\n")
    for s in cands:
        sys.stderr.write(f"  {s}\n")
sys.exit(1)
PY
}

if [ $# -eq 0 ]; then
  pick list
  echo "Play one with: tools/scenario.sh <name>   (start the boat first: tools/scenario.sh up)"
  exit 0
fi
if [ $# -ne 1 ]; then
  echo "usage: tools/scenario.sh [--dry-run] <name> | up | down | status | zones | log | alarm PATH STATE | clear PATH | access" >&2
  exit 2
fi

stem=$(pick "$1")
file="$dir/$stem.json"
if [ "$dry" -eq 1 ]; then
  echo "$file"
  exit 0
fi
echo "overlay $stem"
exec "$py" -I $sk/ctl.py overlay "$stem"
