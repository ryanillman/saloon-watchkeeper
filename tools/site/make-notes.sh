#!/usr/bin/env bash
# Prints the release notes for a tag: the fixed pre-release text in release-notes.md, then the commits since the
# previous tag, grouped by their message prefix. With no argument it previews the notes for HEAD, as if tagged now.
# usage: tools/site/make-notes.sh [tag-or-ref]
set -eu
cd "$(dirname "$0")/../.."
ref=${1:-HEAD}

# The previous tag: look before the ref if it is itself a tag, otherwise from the ref.
if git describe --tags --exact-match "$ref" >/dev/null 2>&1; then
  prev=$(git describe --tags --abbrev=0 "$ref^" 2>/dev/null || true)
else
  prev=$(git describe --tags --abbrev=0 "$ref" 2>/dev/null || true)
fi
range=${prev:+$prev..}$ref

cat tools/site/release-notes.md

git log --no-merges --pretty='%s (%h)' "$range" | awk -v prev="$prev" '
  /^feat(\([^)]*\))?!?:/ { feat = feat "- " $0 "\n"; next }
  /^fix(\([^)]*\))?!?[: ]/ { fix = fix "- " $0 "\n"; next }
  { other = other "- " $0 "\n" }
  END {
    if (feat == "" && fix == "" && other == "") exit
    print "\n## Changes" (prev != "" ? " since " prev : "") "\n"
    if (feat  != "") printf "### Features\n\n%s\n", feat
    if (fix   != "") printf "### Fixes\n\n%s\n", fix
    if (other != "") printf "### Other\n\n%s\n", other
  }'
