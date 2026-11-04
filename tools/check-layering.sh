#!/bin/sh
# roles/ and ui/ must build in the simulator, so they may not include ESP-IDF headers.
cd "$(dirname "$0")/.." || exit 2
dirs=""
for d in components/ui components/roles components/alerts components/history; do [ -d "$d" ] && dirs="$dirs $d"; done
[ -z "$dirs" ] && exit 0
hits=$(grep -rnE '#include *[<"](esp_|freertos/|driver/|sdkconfig)' $dirs)
if [ -n "$hits" ]; then
  echo "ESP-IDF headers in portable components:"
  echo "$hits"
  exit 1
fi
exit 0
