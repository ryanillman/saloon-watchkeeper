#!/bin/sh
# roles/ and ui/ must build in the simulator, so they may not include ESP-IDF headers.
cd "$(dirname "$0")/.." || exit 2
# n2k, n2k_node and n2k_bus are libraries for other firmwares: libc and each other only.
# (n2k_bus's ESP task wrapper, *_task.[ch], is exempt.)
for d in components/n2k components/n2k_node components/n2k_bus; do
  [ -d "$d" ] || continue
  bad=$( { grep -rnE --exclude='*_task.[ch]' '#include *"' "$d" | grep -vE '#include *"n2k'
           grep -rnE --exclude='*_task.[ch]' '#include *<' "$d" | grep -vE '#include *<(stdint|stddef|stdbool|string)\.h>'; } )
  if [ -n "$bad" ]; then
    echo "$d may include only libc (stdint, stddef, stdbool, string) and n2k headers:"
    echo "$bad"
    exit 1
  fi
done
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
