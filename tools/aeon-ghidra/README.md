# aeon-ghidra — headless Ghidra for the MStar AEON scaler core

Containerised Ghidra 12.1.3 plus `eb4x/ghidra-plugin-aeon` (the MStar **AEON R2**
processor module), so we can disassemble the MST9U scaler AP without putting
JDK 21 or Ghidra on the host. **Static analysis only** — the firmware is
disassembled, never executed.

## Setup

```sh
tools/aeon-ghidra/build.sh          # fetch (SHA-pinned) + docker build -> image "aeon-ghidra"
```

Downloads cache in `~/.cache/aeon-ghidra` (override with `AEON_CACHE`). Needs
Docker; ~2 GB image. Both archives are SHA-256 checked at build:

- Ghidra 12.1.3 PUBLIC — `93a5d11a…fd54`
- ghidra-plugin-aeon v0.1.0 (for 12.1.3) — `d76c6a91…2863`

## Disassemble

```sh
tools/aeon-ghidra/run.sh firmware/unpacked/scaler/ap_decompressed.bin
# -> firmware/unpacked/scaler/ap_decompressed.bin.aeon.asm
```

`run.sh <binary> [base-addr] [language]`, defaulting to base `0x180000`
(the header's `ap_ram_start`) and language `AEON:BE:32:R2`. It imports the raw
binary, runs auto-analysis, and writes a full listing via
`scripts/ExportListing.java`. First verified run: **64,525 instructions,
956 functions** auto-detected; prologues (`b.addi r1,r1,-N` / `b.sw …(r1),r9`),
calls (`b.jal`), returns (`b.jr r9`) and `b.movhi`+`b.addi` address pairs all
decode cleanly.

## Notes / limits

- **Endianness:** the plugin ships only `AEON:BE:32:R2`. AEON instructions are
  always big-endian, so code decodes correctly. The core's data is big-endian
  too (OpenRISC-derived); the little-endian values in `MERGE.bin`'s container
  header are the container's, not the AP's.
- **Decompiler:** this image has no `linux_arm_64` decompiler native, so
  Ghidra's C decompiler is unavailable (disassembly is not affected). To get it,
  build the native in-container or run the image under `--platform linux/amd64`
  (slower, via emulation).
- **Memory map:** `b.movhi` targets reach past `0x209c79` (e.g. ~`0x309f7c`),
  i.e. the whole decompressed blob is addressed from `0x180000` upward with BSS
  beyond it. Resolving the exact table/string mapping is follow-up work; see
  `docs/firmware-components.md`.
- Generated `*.aeon.asm` files land under `firmware/` (git-ignored).
- To open the same program in the Ghidra GUI, point a desktop Ghidra 12.1.3 with
  the AEON extension at the binary using the same language/base.
