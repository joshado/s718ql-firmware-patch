# Dell S718QL projector — firmware reverse engineering

Working notes and tooling for understanding (and eventually modifying) the
firmware of the Dell Advanced Projector **S718QL**, a 4K UHD laser DLP
ultra-short-throw projector. The underlying hardware is an **XGIMI** design
(the formatter MCU project is literally named `XGIMI_T1_M451_AppCode`) rebadged
by Dell.

The goal is to understand the architecture well enough to safely patch specific
behaviours. See [`docs/feature-targets.md`](docs/feature-targets.md) for the
concrete wishlist driving this work.

## The firmware package

Everything starts from the last published update:

| | |
|---|---|
| File | `Dell-s718ql-projector-firmware-m0r005-2018-11-30.zip` |
| Dell driver ID | NTDCW (version A00-00, released 07 Jan 2019) |
| SHA-256 | `ef7f01808acc359f496536164e925a2cccca63fc7eb02a0deeae603f00b174ea` |
| Size | 282,275,931 bytes |

The SHA-256 above matches the checksum Dell publishes on the driver page, so the
download is intact. There is an unconfirmed user report of a later **M0R006**
build; worth checking what your unit currently runs before flashing anything.

## Repository layout

```
firmware/
  original/      the Dell .zip (untrusted input; nothing is executed from here)
  extracted/     unzipped package, incl. Dell's 20-page flashing guide (PDF)
  unpacked/      our extracted/decoded views of each firmware image
    scaler/        MStar MST9U sections split out of MERGE.bin
    network/       Actions Micro container records + extracted ext2 filesystems
    actiontec/     Realtek install.img contents (tar) + ext4 images
    mcu/           NXP + Nuvoton microcontroller images as flat binaries
tools/           our scripts (see below); kept separate from untrusted data
docs/            the writeups
decompiled/      hand-decompiled C for reading (not buildable, not verified)
```

> **Safety note on handling the images.** The contents of `firmware/extracted`
> and `firmware/unpacked` are untrusted vendor blobs. Scripts live in `tools/`
> and are always run from there with explicit path arguments, never from inside
> the data directories, and Python is run with `-I` so it can't pick up a
> planted module from the working directory.

## Tools

| Script | Purpose | Status |
|---|---|---|
| `tools/mst9u_merge.py` | Parse/split `MERGE.bin`; verify its CRC-16 + CRC-32 | **round-trips checksums** |
| `tools/act_unpack.py` | Split `ACTUPGRADE.bin` into its component records | working |
| `tools/strsearch.py` | Search blobs for text in ASCII/UTF-16LE/UTF-16BE | working |
| `tools/mst9u_strings.py` | Dump scaler menu text (glyph-index encoded) and plain ASCII strings to TSV | working |
| `tools/aeon-ghidra/` | Containerised headless Ghidra + AEON R2 plugin: disassemble/decompile/assemble the MStar scaler core, exhaustive-disassembly and string-naming scripts | working (see its README) |
| `tools/nxp_disasm.py` | Recursive-descent Thumb disassembly of the NXP MCU image (needs `capstone`) | working |
| `tools/nxp_trace.py` | Condense that listing into per-function GPIO/delay/call traces | working |
| `tools/nxp_initdata.py` | Decompress the NXP app's `.data` initialisers | working |
| `tools/nxp_flash.py` | Flash the NXP app over RS232 (BootCode block protocol); dry-run and simulator self-test | **untested on hardware** |

## Documentation

- [`docs/architecture.md`](docs/architecture.md) — the six-processor system,
  what each does, how they interconnect, and the boot/power sequence.
- [`docs/firmware-components.md`](docs/firmware-components.md) — per-image file
  formats, checksums, and what's inside.
- [`docs/rebuilding.md`](docs/rebuilding.md) — how to reconstruct each image
  (what's solved, what's still open).
- [`docs/feature-targets.md`](docs/feature-targets.md) — the wishlist and where
  in the firmware each item actually lives.
- [`docs/nxp-mcu.md`](docs/nxp-mcu.md) — NXP system MCU disassembly notes: pin
  map, settings block, power state machine, standby modes and WoL.

## Current state (high level)

- **Scaler (`MERGE.bin`)** — layout fully mapped; **all three integrity checks
  reproduced**, including the former unknown header field (`0x1b41bbca` = a
  CRC-32 over the AP flash region). The AP is standard raw DEFLATE. The CPU is
  MStar's AEON 32-bit RISC ("risc32", confirmed); we now have a working
  **disassembler, decompiler and assembler** for it (`tools/aeon-ghidra/`), so
  data edits, in-place code patches and repack/re-sign are all specified. No C
  compiler exists for AEON (new logic = hand-written assembly). Not yet flashed;
  scaler flashing needs an SPI-programmer recovery path first.
- **Network module (`ACTUPGRADE.bin`)** — container is a flat record table with
  a 32-bit additive checksum we reproduce exactly. Linux/MIPS, EZCast Pro stack.
  Filesystems extract cleanly.
- **Actiontec (`install.img`)** — plain tar, `secure_boot=0`. Android/Linux on
  Realtek RTD1295 (ARM64), the ScreenBeam wireless-display subsystem.
- **NXP MCU** — application disassembled; power state machine, standby modes
  and the built-in PHY Wake-on-LAN path mapped (see `docs/nxp-mcu.md`).
- **DDP, Nuvoton MCU** — identified and strings-mined; internals not yet
  mapped.

Nothing has been repacked or flashed yet. All findings so far are from static
analysis of the images only.
