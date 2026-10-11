#!/usr/bin/env bash
# Decompile an AEON binary to C using the amd64 image under emulation (the
# arm64 Ghidra ships no decompiler native). Build it first with:
#   AEON_PLATFORM=linux/amd64 AEON_IMAGE=aeon-ghidra:amd64 tools/aeon-ghidra/build.sh
#
#   decomp.sh <binary> [base-addr] [start] [end]
#
# base-addr defaults 0x180000. start/end (hex) limit decompilation to functions
# whose entry is in [start,end) — recommended, since emulated decompilation of
# the whole image is slow. Output: <binary-dir>/<binary>.aeon.c
set -euo pipefail

bin="${1:?usage: decomp.sh <binary> [base-addr] [start] [end]}"
base="${2:-0x180000}"
start="${3:-}"
end="${4:-}"
image="${AEON_IMAGE:-aeon-ghidra:amd64}"
lang="${AEON_LANG:-AEON:BE:32:R2}"

bin_abs="$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin")"
dir="$(dirname "$bin_abs")"
name="$(basename "$bin_abs")"
out="${name}.aeon.c"

docker run --rm --platform linux/amd64 \
  -v "$dir":/in:ro \
  -v "$dir":/out \
  -v "$(cd "$(dirname "$0")" && pwd)/scripts":/opt/scripts:ro \
  "$image" \
  /work aeon \
  -import "/in/$name" \
  -processor "$lang" \
  -loader BinaryLoader -loader-baseAddr "$base" \
  -scriptPath /opt/scripts \
  -postScript ExportDecomp.java "/out/$out" $start $end \
  -deleteProject -overwrite

echo "decompiled C: $dir/$out"
