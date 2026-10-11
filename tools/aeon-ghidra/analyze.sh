#!/usr/bin/env bash
# Full analysis pipeline for a raw AEON binary: define strings, recover pointer
# references, name functions from referenced strings, and export both a
# disassembly listing and decompiled C. Uses the amd64 image (decompiler).
#
#   analyze.sh <binary> [base-addr]
#
# Outputs next to the binary:
#   <name>.aeon.asm        disassembly (with plate comments of referenced strings)
#   <name>.aeon.c          decompiled C
#   scaler_symbols.tsv     addr / old / new / module / evidence per function
#
# Slow (full decompile under emulation, ~10 min). base-addr defaults 0x180000.
set -euo pipefail

bin="${1:?usage: analyze.sh <binary> [base-addr]}"
base="${2:-0x180000}"
image="${AEON_IMAGE:-aeon-ghidra:amd64}"
lang="${AEON_LANG:-AEON:BE:32:R2}"

bin_abs="$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin")"
dir="$(dirname "$bin_abs")"; name="$(basename "$bin_abs")"

docker run --rm --platform linux/amd64 \
  -v "$dir":/in:ro -v "$dir":/out \
  -v "$(cd "$(dirname "$0")" && pwd)/scripts":/opt/scripts:ro \
  "$image" \
  /work aeon \
  -import "/in/$name" \
  -processor "$lang" \
  -loader BinaryLoader -loader-baseAddr "$base" \
  -scriptPath /opt/scripts \
  -postScript DefineStrings.java \
  -postScript AeonResolvePointers.java \
  -postScript NameFromStrings.java "/out/scaler_symbols.tsv" \
  -postScript ExportListing.java "/out/${name}.aeon.asm" \
  -postScript ExportDecomp.java "/out/${name}.aeon.c" \
  -deleteProject -overwrite

echo "listing:  $dir/${name}.aeon.asm"
echo "C:        $dir/${name}.aeon.c"
echo "symbols:  $dir/scaler_symbols.tsv"
