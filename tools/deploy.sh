#!/bin/sh
# Flash each serial port with the image for that physical board.
# The tty name is not used. The port's USB serial is looked up in tools/board-usb.txt.
# Usage: tools/deploy.sh [--dry-run] /dev/ttyACM0 [/dev/ttyACM1 ...]
# Needs ESP-IDF on PATH: . ~/esp/esp-idf/export.sh
set -eu
cd "$(dirname "$0")/.."

dry=0
if [ "${1:-}" = "--dry-run" ]; then
  dry=1
  shift
fi
if [ $# -eq 0 ]; then
  echo "usage: tools/deploy.sh [--dry-run] /dev/ttyACM0 [/dev/ttyACM1 ...]" >&2
  exit 2
fi

map=tools/board-usb.txt
if [ ! -f "$map" ]; then
  echo "missing $map" >&2
  exit 1
fi

# Reject a bad map before opening any port.
while read -r key name extra; do
  case $key in
    ''|'#'*) continue ;;
  esac
  if [ -z "$name" ] || [ -n "$extra" ]; then
    echo "bad line in $map: $key ${name:-} ${extra:-}" >&2
    exit 1
  fi
  if [ ! -f "boards/$name/sdkconfig.defaults" ]; then
    echo "$map: '$name' is not a board under boards/" >&2
    exit 1
  fi
done < "$map"

identify() {
  port=$1
  props=$(udevadm info -q property -n "$port" 2>/dev/null) || {
    echo "udevadm cannot read $port" >&2
    return 1
  }
  serial=$(printf '%s\n' "$props" | sed -n 's/^ID_SERIAL=//p' | head -n 1)
  short=$(printf '%s\n' "$props" | sed -n 's/^ID_SERIAL_SHORT=//p' | head -n 1)
  serial_u=$(printf '%s' "$serial" | tr '[:lower:]' '[:upper:]')
  short_u=$(printf '%s' "$short" | tr '[:lower:]' '[:upper:]')
  if [ -z "$serial_u" ] && [ -z "$short_u" ]; then
    echo "no USB serial for $port" >&2
    return 1
  fi

  found=""
  while read -r key name extra; do
    case $key in
      ''|'#'*) continue ;;
    esac
    key_u=$(printf '%s' "$key" | tr '[:lower:]' '[:upper:]')
    if [ "$key_u" = "$short_u" ] || [ "$key_u" = "$serial_u" ]; then
      found=$name
      break
    fi
  done < "$map"

  if [ -z "$found" ]; then
    echo "no board for $port (ID_SERIAL=${serial:-?} ID_SERIAL_SHORT=${short:-?})" >&2
    echo "add a line to $map" >&2
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
for port in "$@"; do
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
  board=$(identify "$port")
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
