#!/bin/sh
# Regenerates the big-digit fonts the Depth page uses (LVGL's built-in Montserrat stops at 48 pt). Digits, the
# decimal point and space only, 4 bpp, so each is a few KB. Needs node (npx fetches lv_font_conv).
# usage: tools/gen-digits-font.sh   (run from anywhere; the TTF is the one LVGL ships)
set -e
cd "$(dirname "$0")/.."
ttf=$(find managed_components build/sim/_deps -name Montserrat-Medium.ttf 2>/dev/null | head -1)
[ -n "$ttf" ] || { echo "Montserrat-Medium.ttf not found: build the simulator or firmware first" >&2; exit 1; }
for size in 96 128; do
  npx --yes lv_font_conv@1.5.3 --font "$ttf" --size "$size" --bpp 4 --no-compress --format lvgl \
    --lv-font-name "ui_font_digits_$size" --symbols "0123456789. " --lv-include lvgl.h \
    -o "components/ui/src/ui_font_digits_$size.c"
done
