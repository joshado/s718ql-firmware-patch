#!/usr/bin/env bash
# Disassemble a raw AEON R2 binary with the headless container and write a
# listing next to it.
#
#   run.sh <binary> [base-addr] [language]
#
# Defaults suit the MST9U scaler AP:
#   base-addr  0x180000     (header ap_ram_start)
#   language   AEON:BE:32:R2 (big-endian; the only variant this plugin ships)
#
# Example:
#   run.sh firmware/unpacked/scaler/ap_decompressed.bin
# Output: <binary-dir>/<binary>.aeon.asm  and the Ghidra summary on stdout.
set -euo pipefail

bin="${1:?usage: run.sh <binary> [base-addr] [language]}"
base="${2:-0x180000}"
lang="${3:-AEON:BE:32:R2}"
image="${AEON_IMAGE:-aeon-ghidra}"

bin_abs="$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin")"
dir="$(dirname "$bin_abs")"
name="$(basename "$bin_abs")"
out="${name}.aeon.asm"

docker run --rm \
  -v "$dir":/in:ro \
  -v "$dir":/out \
  -v "$(cd "$(dirname "$0")" && pwd)/scripts":/opt/scripts:ro \
  "$image" \
  /work aeon \
  -import "/in/$name" \
  -processor "$lang" \
  -loader BinaryLoader -loader-baseAddr "$base" \
  -scriptPath /opt/scripts \
  -postScript ExportListing.java "/out/$out" \
  -deleteProject -overwrite

echo "listing: $dir/$out"
