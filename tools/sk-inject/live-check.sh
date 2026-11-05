#!/bin/sh
# Live check of the whole alarm loop against the local Signal K (localhost:3000):
#   boat sends routine values -> the port coolant is driven into its alarm zone ->
#   the SERVER raises the notification -> the value returns to normal -> the server clears it.
# Skips (exit 0) when there is no server or no token. Exits 1 on a failed check.
#   tools/sk-inject/live-check.sh
set -eu
cd "$(dirname "$0")/../.."

server=localhost:3000
path=propulsion.port.temperature
url="http://$server/signalk/v1/api/vessels/self/notifications/propulsion/port/temperature"

if ! curl -s -m 3 -o /dev/null "http://$server/signalk"; then
  echo "skip: no Signal K server on $server"; exit 0
fi
if [ ! -f tools/sk-inject/.token ]; then
  echo "skip: no token (tools/scenario.sh access)"; exit 0
fi

started=0
if ! tools/scenario.sh status >/dev/null 2>&1; then
  tools/scenario.sh up >/dev/null
  started=1
fi
cleanup() {
  tools/scenario.sh clear "$path" >/dev/null 2>&1 || true
  if [ "$started" -eq 1 ]; then tools/scenario.sh down >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT

state() { curl -s -m 3 "$url" | sed -n 's/.*"state":"\([a-z]*\)".*/\1/p' | head -n 1; }

echo "raising $path 'alarm' through the server's zones"
if ! tools/scenario.sh alarm "$path" alarm; then
  echo "FAIL: alarm step did not report the server raising it" >&2; exit 1
fi
s=$(state)
[ "$s" = "alarm" ] || { echo "FAIL: server notification is '$s', expected 'alarm'" >&2; exit 1; }
echo "ok: server notification is 'alarm'"

tools/scenario.sh clear "$path" >/dev/null
i=0
while [ $i -lt 20 ]; do
  s=$(state)
  [ "$s" != "alarm" ] && break
  i=$((i + 1)); sleep 0.5
done
[ "$s" != "alarm" ] || { echo "FAIL: server still shows 'alarm' 10 s after clear" >&2; exit 1; }
echo "ok: cleared, server notification is '$s'"
echo "live check passed"
