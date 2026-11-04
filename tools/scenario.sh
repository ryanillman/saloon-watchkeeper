#!/bin/sh
# Play a Signal K scenario by name, or by a prefix that matches one file.
#   tools/scenario.sh                  list them
#   tools/scenario.sh fuel             play alarm-fuel-low
#   tools/scenario.sh --dry-run fuel   print the file, do not play
#   tools/scenario.sh access           request a write token (see docs/testing.md)
set -eu
cd "$(dirname "$0")/.."

# The firmware shell puts the ESP-IDF virtualenv first on PATH. That Python
# does not have the websockets package inject.py needs. Use the system one.
py=/usr/bin/python3

dry=0
if [ "${1:-}" = "--dry-run" ]; then
  dry=1
  shift
fi
if [ "${1:-}" = "access" ]; then
  exec "$py" -I tools/sk-inject/inject.py access
fi

dir=tools/sk-inject/scenarios

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
    sys.stderr.write(f"no scenario matches '{cmd}'\n")
else:
    sys.stderr.write(f"'{cmd}' matches more than one scenario:\n")
    for s in cands:
        sys.stderr.write(f"  {s}\n")
sys.exit(1)
PY
}

if [ $# -eq 0 ]; then
  pick list
  echo "Play one with: tools/scenario.sh <name>"
  exit 0
fi
if [ $# -ne 1 ]; then
  echo "usage: tools/scenario.sh [--dry-run] <name>" >&2
  exit 2
fi

stem=$(pick "$1")
file="$dir/$stem.json"
if [ "$dry" -eq 1 ]; then
  echo "$file"
  exit 0
fi
if ! "$py" -I -c 'import websockets' >/dev/null 2>&1; then
  echo "Python is missing the websockets package inject.py needs." >&2
  echo "Install it with: sudo apt install python3-websockets" >&2
  exit 1
fi
echo "playing $stem"
export PYTHONUNBUFFERED=1
exec "$py" -I tools/sk-inject/inject.py "$file"
