#!/usr/bin/env bash
# Build the aeon-ghidra image: fetch Ghidra 12.1.3 + the AEON R2 plugin
# (SHA-pinned), assemble a build context, and docker build.
# Downloads are cached in $AEON_CACHE (default ~/.cache/aeon-ghidra) and reused.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cache="${AEON_CACHE:-$HOME/.cache/aeon-ghidra}"
image="${AEON_IMAGE:-aeon-ghidra}"

GHIDRA_URL="https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.3_build/ghidra_12.1.3_PUBLIC_20260817.zip"
GHIDRA_SHA="93a5d11a9ad510622acaaf908c556a7b9b764d338e78a7567f3689bf5081fd54"
AEON_URL="https://github.com/eb4x/ghidra-plugin-aeon/releases/download/v0.1.0/ghidra_12.1.3_PUBLIC_20260918_AeonR2.zip"
AEON_SHA="d76c6a914cf876e9913c0d4c3596c2280d0a8f08bc08dd02fdf8003304562863"

fetch() { # url sha dest
  local url="$1" sha="$2" dest="$3"
  if [ -f "$dest" ] && echo "$sha  $dest" | shasum -a 256 -c - >/dev/null 2>&1; then
    echo "cached: $dest"; return
  fi
  echo "downloading: $url"
  curl -fsSL -o "$dest" "$url"
  echo "$sha  $dest" | shasum -a 256 -c -
}

mkdir -p "$cache"
fetch "$GHIDRA_URL" "$GHIDRA_SHA" "$cache/ghidra.zip"
fetch "$AEON_URL"   "$AEON_SHA"   "$cache/aeon.zip"

ctx="$(mktemp -d)"
trap 'rm -rf "$ctx"' EXIT
cp "$here/Dockerfile" "$ctx/"
cp -r "$here/scripts" "$ctx/"
cp "$cache/ghidra.zip" "$cache/aeon.zip" "$ctx/"

echo "building image: $image"
docker build -t "$image" "$ctx"
echo "done: $image"
