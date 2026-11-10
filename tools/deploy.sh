#!/bin/sh
# Flash each serial port with the image for that physical board.
# The tty name is not used: each board is asked which it is (see tools/board-id.py), which resets it.
# Usage: tools/deploy.sh [--dry-run] <port | board=port> ...    e.g. /dev/ttyACM0 lcd7=/dev/ttyACM1
# --dry-run only identifies the boards (it still resets them).
# Needs ESP-IDF on PATH: . ~/esp/esp-idf/export.sh
set -eu
cd "$(dirname "$0")/.."

dry=0
if [ "${1:-}" = "--dry-run" ]; then
  dry=1
  shift
fi
if [ $# -eq 0 ]; then
  echo "usage: tools/deploy.sh [--dry-run] <port | board=port> ..." >&2
  exit 2
fi

# The firmware on each port names its board in the boot log (tools/board-id.py resets the chip and reads it), so no
# serial-number table is needed. A blank chip, or one running other firmware, cannot say: name it as board=port.
identify() {
  port=$1
  if ! python3 -c 'import serial' 2>/dev/null; then
    echo "pyserial is missing. Run: . ~/esp/esp-idf/export.sh" >&2
    return 1
  fi
  found=$(python3 "$(dirname "$0")/board-id.py" "$port") || {
    echo "name it yourself: tools/deploy.sh <board>=$port" >&2
    return 1
  }
  if [ ! -f "boards/$found/sdkconfig.defaults" ]; then
    echo "$port reports board '$found', which is not a directory under boards/" >&2
    return 1
  fi
  printf '%s\n' "$found"
}

if [ "$dry" -eq 0 ]; then
  if ! command -v idf.py >/dev/null 2>&1; then
    echo "idf.py is not on PATH. Run: . ~/esp/esp-idf/export.sh" >&2
    exit 1
  fi
fi

jobs=$(mktemp)
logdir=
trap 'rm -f "$jobs"; [ -n "$logdir" ] && rm -rf "$logdir"' EXIT

seen=""
for arg in "$@"; do
  named=""
  port=$arg
  case $arg in
    *=*) named=${arg%%=*}; port=${arg#*=} ;;
  esac
  if [ -n "$named" ] && [ ! -f "boards/$named/sdkconfig.defaults" ]; then
    echo "'$named' is not a board under boards/" >&2
    exit 1
  fi
  if [ ! -e "$port" ]; then
    echo "no such port: $port" >&2
    exit 1
  fi
  canon=$(readlink -f "$port")
  case " $seen " in
    *" $canon "*)
      echo "port listed twice: $port" >&2
      exit 1
      ;;
  esac
  seen="$seen $canon"
  if [ -n "$named" ]; then board=$named; else board=$(identify "$port") || exit 1; fi
  printf '%s %s\n' "$board" "$canon" >> "$jobs"
  echo "$board $port"
done

if [ "$dry" -eq 1 ]; then
  exit 0
fi

missing=0
while read -r board port; do
  if [ ! -f "build/$board/saloon_watchkeeper.bin" ]; then
    echo "no image for $board (build/$board/saloon_watchkeeper.bin). Run tools/build-firmware.sh" >&2
    missing=1
  fi
done < "$jobs"
[ "$missing" -eq 0 ] || exit 1

logdir=$(mktemp -d)

# One worker per board, so two ports of the same board do not share a build dir.
# Different boards flash at the same time.
boards=$(cut -d' ' -f1 "$jobs" | sort -u)
pids=""
for board in $boards; do
  (
    while read -r b port; do
      [ "$b" = "$board" ] || continue
      name=$(basename "$port")
      echo "flashing $board -> $port"
      if ! idf.py -B "build/$board" -p "$port" flash > "$logdir/$board-$name.log" 2>&1; then
        echo "flash failed: $board on $port" >&2
        tail -n 30 "$logdir/$board-$name.log" >&2
        exit 1
      fi
      echo "flashed $board on $port"
    done < "$jobs"
  ) &
  pids="$pids $!"
done

fail=0
for pid in $pids; do
  if ! wait "$pid"; then
    fail=1
  fi
done
exit "$fail"
