#!/usr/bin/env bash
# Regenerate the named disassembly + decompilation of the scaler AP, applying
# the curated symbol table (symbols.tsv). The symbol table is the persistent
# store: edit it, re-run this, and the names reappear in the output.
#
#   regen.sh [binary] [base] [dstart] [dend]
#
# Pipeline: exhaustive disassembly -> pointer resolution -> define strings ->
# auto-name from strings -> APPLY curated symbols (override) -> export .asm + .c.
# Defaults: scaler AP, base 0x180000, decompile code region 0x180000-0x230000.
# The decompile is slow under emulation (amd64 image); narrow dstart/dend while
# iterating. Outputs (next to the binary, git-ignored):
#   <name>.aeon.asm          named listing
#   <name>.aeon.c            named decompilation
#   scaler_symbols_auto.tsv  what the auto-namer found (for merging into symbols.tsv)
#   symbols_dump.tsv         all user-defined names after apply (round-trip capture)
set -euo pipefail

bin="${1:-firmware/unpacked/scaler/ap_combined.bin}"
# The RAM image is the 0x3800 uncompressed prefix + the inflated stream. Loading
# the inflated stream alone at 0x180000 puts every absolute address off by 0x3800
# (the prefix size), so build/refresh the combined image here.
sd="firmware/unpacked/scaler"
if [ ! -f "$sd/ap_combined.bin" ] || [ "$sd/ap_decompressed.bin" -nt "$sd/ap_combined.bin" ]; then
  cat "$sd/ap_prefix.bin" "$sd/ap_decompressed.bin" > "$sd/ap_combined.bin"
fi
base="${2:-0x180000}"
dstart="${3:-0x180000}"
dend="${4:-0x230000}"
here="$(cd "$(dirname "$0")" && pwd)"
image="${AEON_IMAGE:-aeon-ghidra:amd64}"

bin_abs="$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin")"
dir="$(dirname "$bin_abs")"; name="$(basename "$bin_abs")"

docker run --rm --platform linux/amd64 \
  -v "$dir":/in:ro -v "$dir":/out \
  -v "$here/scripts":/opt/scripts:ro \
  -v "$here":/opt/aeon:ro \
  "$image" \
  /work aeon \
  -import "/in/$name" \
  -processor AEON:BE:32:R2 \
  -loader BinaryLoader -loader-baseAddr "$base" \
  -scriptPath /opt/scripts \
  -postScript AeonFlowDisasm.java "$dstart" "$dend" \
  -postScript AeonFoldRefs.java \
  -postScript DefineStrings.java \
  -postScript NameFromStrings.java "/out/scaler_symbols_auto.tsv" \
  -postScript ApplySymbols.java /opt/aeon/symbols.tsv \
  -postScript ExportListing.java "/out/${name}.aeon.asm" \
  -postScript ExportDecomp.java "/out/${name}.aeon.c" "$dstart" "$dend" \
  -postScript DumpSymbols.java "/out/symbols_dump.tsv" \
  -deleteProject -overwrite

echo "listing: $dir/${name}.aeon.asm"
echo "C:       $dir/${name}.aeon.c"
echo "auto:    $dir/scaler_symbols_auto.tsv   (merge useful ones into tools/aeon-ghidra/symbols.tsv)"
