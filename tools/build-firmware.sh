#!/bin/sh
# Build every supported board (each directory in boards/ with sdkconfig.defaults),
# or the boards named on the command line.
# Writes build/<board>/ and a merged image at out/<board>.bin (flash at 0x0).
# Usage: tools/build-firmware.sh [square4 lcd7 ...]
# Needs ESP-IDF on PATH: . ~/esp/esp-idf/export.sh
set -eu
cd "$(dirname "$0")/.."

if [ $# -eq 0 ]; then
  # shellcheck disable=SC2046
  set -- $(find boards -mindepth 1 -maxdepth 1 -type d -exec test -f '{}/sdkconfig.defaults' \; -printf '%f\n' | sort)
fi
if [ $# -eq 0 ]; then
  echo "no boards under boards/" >&2
  exit 1
fi

for b in "$@"; do
  if [ ! -f "boards/$b/sdkconfig.defaults" ]; then
    echo "unknown board: $b" >&2
    exit 1
  fi
done

if ! command -v idf.py >/dev/null 2>&1; then
  echo "idf.py is not on PATH. Run: . ~/esp/esp-idf/export.sh" >&2
  exit 1
fi
if ! command -v esptool.py >/dev/null 2>&1; then
  echo "esptool.py is not on PATH. Run: . ~/esp/esp-idf/export.sh" >&2
  exit 1
fi

mkdir -p out
for b in "$@"; do
  echo "building $b"
  idf.py -D BOARD="$b" -B "build/$b" build
  (cd "build/$b" && esptool.py --chip esp32s3 merge_bin -o "../../out/$b.bin" @flash_args)
  echo "out/$b.bin"
done
