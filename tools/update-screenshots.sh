#!/bin/sh
# Re-renders the README screenshots from the simulator into docs/images/.
# They are simulator renders of fixture data, not photos of a boat.
# Usage: tools/update-screenshots.sh            (needs the sim built: cmake --build build/sim)
set -eu
cd "$(dirname "$0")/.."
sim=build/sim/sim
[ -x "$sim" ] || { echo "build the simulator first (see README)" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$sim" --snapshot "$tmp"
mkdir -p docs/images
# <name in docs/images> <board>/<palette>/<snapshot file>
while read -r name src; do
  [ -n "$name" ] || continue
  cp "$tmp/$src" "docs/images/$name.png"
done <<LIST
glance            sim800/day/glance-p0-live.png
engine            sim800/day/engine-p0-live.png
engine-fail       sim800/day/engine-p0-fault.png
power             sim800/day/power-p0-banks-live.png
navigation        sim800/day/navigation-p0-full.png
anchor            sim800/day/anchor-p0-holding.png
depth             sim800/day/depth-p0-live.png
glance-night      sim1024/night/glance-p0-live.png
glance-4in        sim480/day/glance-p0-live.png
anchor-4in        sim480/day/anchor-p0-holding.png
LIST
echo "docs/images updated"
