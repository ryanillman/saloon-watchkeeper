#!/bin/sh
# Adds one channel (release or latest) to the install site in <site>: copies the merged images, writes an ESP Web Tools
# manifest per board and info.json, and refreshes index.html. The other channel's files are left alone.
# Usage: tools/site/build-site.sh <site dir> <release|latest> <version> <commit> <images dir>
# The images dir holds <board>.bin, as tools/build-firmware.sh writes to out/.
set -eu
site=$1 channel=$2 version=$3 commit=$4 images=$5
here=$(cd "$(dirname "$0")" && pwd)

case "$channel" in release | latest) ;; *) echo "channel must be release or latest" >&2; exit 1 ;; esac
set -- "$images"/*.bin
[ -e "$1" ] || { echo "no images in $images" >&2; exit 1; }

rm -rf "${site:?}/$channel"
mkdir -p "$site/$channel"
boards=""
for f in "$images"/*.bin; do
  b=$(basename "$f" .bin)
  cp "$f" "$site/$channel/$b.bin"
  cat > "$site/$channel/$b.manifest.json" <<JSON
{
  "name": "Saloon Watchkeeper ($b), pre-release $version",
  "version": "$version",
  "new_install_prompt_erase": false,
  "builds": [
    { "chipFamily": "ESP32-S3", "parts": [ { "path": "$b.bin", "offset": 0 } ] }
  ]
}
JSON
  boards="$boards\"$b\","
done
boards="[${boards%,}]"
printf '{ "version": "%s", "commit": "%s", "built": "%s", "boards": %s }\n' \
  "$version" "$commit" "$(date -u +%Y-%m-%dT%H:%MZ)" "$boards" > "$site/$channel/info.json"
(cd "$site/$channel" && sha256sum ./*.bin > SHA256SUMS)

cp "$here/index.html" "$site/index.html"
touch "$site/.nojekyll"
