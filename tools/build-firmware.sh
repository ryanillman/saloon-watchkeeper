#!/bin/sh
# Builds one firmware image per board and a merged .bin (flash at 0x0) in out/.
# Usage: tools/build-firmware.sh [square4 lcd7 lcd7b]
set -e
cd "$(dirname "$0")/.."
boards="${*:-square4 lcd7 lcd7b}"
mkdir -p out
for b in $boards; do
  idf.py -D BOARD="$b" -B "build/$b" build
  (cd "build/$b" && esptool.py --chip esp32s3 merge_bin -o "../../out/$b.bin" @flash_args)
  echo "out/$b.bin"
done
