# Firmware components — formats and contents

Everything below is from the M0R005 package. Offsets/sizes are from the actual
files. "Confirmed" = reproduced in code or extracted cleanly; "inferred" = read
from structure/strings only.

The zip expands to one folder with six sub-folders plus Dell's guide:

```
Dell-S718QL-firmware-upgrade-setup-guide-a02-Jul-2018.pdf   (the flashing guide)
Projector FW/   MERGE.bin                       1,473,068 B   scaler
DDP FW/         Dell-s718ql-ddp-firmware-a30-2018-10-23.img 14,368,268 B  formatter DLPC
                DLPC4422-GUI/DLPC4422-GUI-v1.0-Setup.exe     (TI Windows flasher)
Network FW/     ACTUPGRADE.bin                269,646,848 B   Actions Micro SoC
Actiontec FW/   install.img                  177,213,472 B   Realtek SoC (tar)
                bluecore.audio, emmc.uImage, rescue.* ...    (loose recovery bits)
NXP MCU FW/     Dell-s718ql-mcu-firmware-e01-2018-11-30.hex    109,550 B  NXP M0
                PIC_NXP_Downloader/PIC_NXP_Downloader.exe
Formatter MCU FW/ ISP/S718QL_FMCU_A10_ISP_20171023.isp          32,676 B Nuvoton M4
                ISP/NuvoISP.exe, ISP/config.ini
```

Note the versions quoted in the guide differ slightly from the files shipped
(guide mentions DDP a28 / MCU a13; package ships DDP a30 / MCU e01) — the guide
is a template reused across releases.

---

## 1. Projector FW — `MERGE.bin` (MStar MST9U scaler) — CONFIRMED

Parsed and checksum-verified by `tools/mst9u_merge.py`. Layout:

| Region | Offset | Notes |
|---|---|---|
| **PM / 8051 code** | `0x000000`–`0x010000` | standby power-management core |
| **Header** | `0x010000` | 10×u32 LE (see below) |
| **sboot (loader)** | `0x010000`–`0x020080` | contains the zlib/inflate used to unpack the AP; build stamp `Feb 02 2017 11:27:01` at 0x20060 |
| **AP blob prefix** | `0x020080`–`0x023880` | **uncompressed** 0x3800 bytes: exception vectors, image info header at +0x1100 (see below), a resource table tagged `TEL` at +0x2000 |
| **AP DEFLATE stream** | `0x023880`–`0x0a9cf3` | raw DEFLATE → **0x181560 bytes** of ARM-ish MStar AP code + strings |
| **AP size + CRC** | `0x0a9cf3` | u32 LE decompressed size, then u16 **CRC-16/CCITT-FALSE** (LE) over `[0x20080 .. end-of-stream)` |
| **tail resources** | `0x0a9cf9`–EOF | referenced by the TEL table; includes the Dell-logo JPEG at `0xb9d08` (TEL type 0xc5, 2-tone, extracted to `firmware/unpacked/scaler/images/`; used as a mask for the no-signal screen — see feature-targets.md (a)) |
| **Trailer** | EOF-12 | `55 AA AB CD` magic; **CRC-32** (poly 0x04C11DB7, MSB-first, init 0, no final XOR) over everything before the last 4 bytes, stored **big-endian** |

Header u32s at 0x10000 (LE): `ap_flash_start=0x20080`, `ap_ram_start=0x180000`,
`ap_ram_end=0x209c79`, `ap_flash_end=0xa9cf9`, `0`, `0x1b41bbca` (**solved** —
CRC-32-MSB over the AP flash region `[0x20080..0xa9cf9)`; see below), `0`, `0`,
`filesize-4=0x167a28`, `0x10000`.

**Image info header** (AP prefix +0x1100, i.e. `MERGE.bin` `0x21180`, 0x50
bytes; earlier notes called this the "panel-info record"). It identifies the
build and repeats two sizes; it does **not** hold display timings.

| Offset | Bytes | Reading |
|---|---|---|
| +0x00 | `78 56 AA 55` | magic `0x55AA5678` |
| +0x0C | `B9 14 00 00` | `0x14B9`, unknown |
| +0x14 | `01 00 01 00 01 00` | three u16 = 1, unknown |
| +0x1A | `DC FE 09 00` | `0xFEDC`, 9, unknown |
| +0x1E | `L-QMD55MWWAC-3333` | model/board identifier of this build |
| +0x32 | `PANEL_NAME` | literal placeholder, never filled in |
| +0x47 | `BE AD DE` | part of a `0xDEADBE..` marker |
| +0x4A | `79 9C 08 00` | `0x89C79` = AP length in flash (`0xA9CF9` − `0x20080`) |
| +0x4E | `00 38` | `0x3800` = size of the uncompressed prefix |

**All three integrity fields are reproduced** — no unknowns remain:
- AP **CRC-16/CCITT-FALSE** `0x679f` over `[0x20080 .. stream_end)`;
- header[5] **CRC-32** (MSB-first, poly `0x04C11DB7`, init 0, no XOR) = `0x1b41bbca`
  over the AP flash region `[0x20080 .. 0xa9cf9)` — the field formerly marked
  "unknown", now cracked (same CRC family as the trailer);
- trailer **CRC-32** `0x2d384606` (same algorithm, stored big-endian) over
  everything but the last 4 bytes.

The AP deflate stream is **standard raw DEFLATE** (Python `zlib -15` inflates it
to our exact `ap_decompressed.bin`), so it can be regenerated. See
[rebuilding.md](rebuilding.md) §1 for the full repack recipe.

**Strings of interest** (all in the decompressed AP): OSD menu text, `Dell
S718QL`, keystone (`dvOmnitek_*Keystone_Write` → an **Omnitek** warp block),
`api_CEC_ProcessCommand` / `monitor_ap/DRIVER/mapi_CEC.c` (CEC lives here),
`PowerOffSystem`, `enter ePM_POWER_DPMS`, logo-capture functions, and the USB
upgrade engine (`MW_UsbDownload_*`, `MERGE.BIN`, `Merge2.bin`).

### Program code (`ap_code.bin`, `0x0`–`0xD0000`)

The first ~832 KB of the decompressed AP, split out as
`firmware/unpacked/scaler/ap_code.bin` for inspection. High-level structure,
from debug strings and statistics (and now a working disassembler/decompiler —
see "Tooling for this core" below):

**CPU — confirmed.** MStar's proprietary **32-bit RISC core** ("risc32", the
AEON family). Evidence in the image: the source path
`./cpu/risc32/MST9U/interrupt.c`, the symbol `_HAL_SetAeon_SPIMappingAddr`,
and `Sboot Chip ID`. Not ARM/MIPS. Absolute 32-bit pointers are rare in the
code (~1% of aligned words fall in the AP RAM range), i.e. the core builds
addresses from immediate pairs (`movhi`+`addi`/`ori`) rather than a literal
pool — typical of this RISC. We now have a disassembler, decompiler **and
assembler** for it (`tools/aeon-ghidra/`); the remaining obstacles to
code-level work are the lack of a C **compiler** for AEON (new logic must be
hand-written assembly) and that AEON pointer references need our own resolution
pass, not the "no disassembler" problem noted earlier.

**It is an MStar "monitor_ap" application.** Source paths and log tags reveal a
layered build (vendor framework, not a general-purpose OS):

| Layer | Evidence | Role |
|---|---|---|
| `cpu/risc32/MST9U/` | `interrupt.c`, `timer_cb/` | core: interrupt vectors, timers |
| `monitor_ap/KERNEL/SYSTEM/` | `Debug.c`, `hwi2c.c` | kernel: logging, I2C bus |
| `monitor_ap/DRIVER/` | `msScaler.c`, `mapi_CEC.c` | drivers: scaler, HDMI-CEC |
| `monitor_ap/CUSTOM/MSTAR/USERFUN/` | `MenuFunc.c` | customer layer: OSD menu actions |
| `MApi_*` / `MDrv_*` / `msAPI_*` | prefixes in strings | MStar API / driver / app-API layers |

**Subsystems** (from `[TAG]` log prefixes, by frequency): `DB_PxP` /
`appWindow` / `msScaler` (the display pipe: picture-in-picture, windows,
scaling, frame-sync); `PQ` and its many sub-tags `PQ_Init`, `PQ_LoadUserTiming`,
`PQ_LoadScalingTable`, `NR`, `MPEGNR`, `SR`, `OD` (picture quality: gamma,
colour, noise reduction, super-resolution, overdrive); `appSystem` / `PM`
(system and power management); `MSFLASH` (SPI flash / the USB upgrade engine,
`MW_UsbDownload_*`); `CEC`; `PANEL` / `LVDS` / timing (`FindMode`,
`LoadTiming`); `dvOmnitek_*` (the Omnitek keystone/warp block); `OSD2`
(on-screen display); `VK` (remote/virtual keys); `GPO0`–`GPOA` (GPIO outputs).

The image also carries a **VESA MCCS/DDC descriptor** (`prot(monitor)type(lcd)
MStar cmds(...)vcp(...)mccs_ver(2.1)`) and a panel id `MST9U3_PanelCMIM236HGJ_L21`.

**Tooling for this core — it exists.** The MST9U AP core is MStar's AEON
(AEON R2, OpenRISC-derived), and there is public RE support:

- **`eb4x/ghidra-plugin-aeon`** (Apache-2.0) — a Ghidra SLEIGH processor module
  for AEON R2. The README lists LE/BE/harvard variants, but the **shipped v0.1.0
  release only contains `AEON:BE:32:R2`** (what we use; AEON instructions are
  big-endian regardless). Integer
  core, branches, loads/stores, `movhi` and system instructions have real
  p-code; MAC/DSP/SIMD/float/cache decode (right length + operands) but are
  pseudo-ops. The author reports it decodes **"the MST9U main firmware"** with
  zero diffs vs vendor objdump over ~1.2M instructions, semantics checked
  against the vendor simulator. Build/install via `./gradlew buildExtension`.
- **`shinyquagsire23/ghidra-aeon`** — the original AEON Ghidra port (from LG
  DualUp / MST9W scaler work); WIP, a few unknown/guessed instructions.
- **Reko decompiler** — has partial AEON support.
- **`vladikit-bit/projector-diagnostic-tools`** — patched `aeon_ghidra_public`,
  Reko projects, Ghidra runners and patch builders (unverified by us).
- Architecture notes: `linux-chenxing.org` (MStar/SigmaStar community).

AEON instructions are **big-endian**; the plugin ships only `AEON:BE:32:R2`,
which decodes the code correctly. (The core itself is OpenRISC-derived and
big-endian; the little-endian words in `MERGE.bin` are the container header,
not the AP.)
Instructions are 2/3/4 bytes (length from the top bits of byte 0), byte
aligned, 32 GPRs with r0 = 0, no delay slots, one condition flag. The
`movhi`+`ori`-style address building is exactly the "immediate pairs, few
absolute pointers" pattern seen above.

**Tooling is set up.** `tools/aeon-ghidra/` runs this plugin in headless Ghidra
via Docker (`build.sh` then `run.sh <binary>`); no host JDK/Ghidra needed. A
verified run over `ap_decompressed.bin` gave **64,525 instructions / 956
functions** of clean AEON code. The C **decompiler** also works, via an amd64
image under emulation (`tools/aeon-ghidra/decomp.sh`). Note the shipped plugin
only has the **`AEON:BE:32:R2`** language (instructions are big-endian anyway);
load at base **`0x180000`** (the header's `ap_ram_start`). The reset/exception
vectors are at the start of the AP prefix (`0x180100`). The DSP/SIMD pseudo-ops
and the unresolved table mapping (below) are the known gaps.

**Disassembly coverage.** Ghidra's default AEON auto-analysis follows only
entry-reachable flow and disassembled just ~35% of the code (64,525 insns / 956
functions). `tools/aeon-ghidra/scripts/DisassembleCodeRegion.java` fills the gaps
by linear-sweeping the code region, taking it to **232k instructions / 2,690
functions** — effectively full code coverage, which is what the string/reference
search needs. Caveat: the blind sweep also disassembles data regions as junk
instructions and can mis-set some function boundaries, so the **decompiler** is
best pointed at a specific function from the default flow-reachable analysis;
use the swept listing for reference/location work (my Python fold over the
listing), not for clean whole-program decompilation.

**Symbol recovery (partial).** `tools/aeon-ghidra/analyze.sh` names functions
from the debug strings they reference. Yield is low — ~10 auto-named, ~37
annotated with their strings (`scaler_symbols.tsv`) — because Ghidra has no
AEON constant-reference analyzer and the `movhi`+`addi` string pointers mostly
stay unresolved (only the ~37-46 the decompiler folds are usable), and the
debug strings are a packed blob indexed mid-way. Useful anchors recovered: PQ
and GPO/Panel timing dumps, `MONITOR_FLAGS`, `USER_PREF_WIN_SEL`, panel
power-off, the SERFLASH driver. Fuller naming needs a real AEON split-immediate
reference pass. The string *content* is fully available regardless via
`tools/mst9u_strings.py` and the defined-string listing.

**Memory map — refined by the disassembly.** The whole decompressed blob maps
contiguously from `0x180000` (file offset `N` → RAM `0x180000 + N`): the
`b.movhi`+`b.addi` address pairs reach data at e.g. `0x309F84`, past the image
end (`0x301560`) into BSS. So the earlier reading — that only the first
`0x89C79` was "live" — was wrong; that range (the header's AP RAM end
`0x209C79`) is just the initialised code+data, with the tables (`0x124000`+),
menu text (`0x164000`+) and fonts continuing in the same flat space and BSS
beyond the image. Exact confirmation (and whether any region is relocated) is
follow-up work with the disassembly, but table offsets can now be read as
`0x180000 + file_offset`.

### Layout of the decompressed AP (`ap_decompressed.bin`, 0x181560 bytes)

From a statistical survey (entropy and text density per 16 KB block, plus spot
samples). Boundaries are on 16 KB steps and the table labels are guesses.

| Range | Size | What it looks like |
|---|---|---|
| `0x000000`–`0x0D0000` | 832 KB | dense binary, almost no text: program code |
| `0x0D0000`–`0x0F0000` | 128 KB | numeric tables; starts with a smoothly rising u16 series (a curve / LUT), then mostly zeros |
| `0x0F0000`–`0x108000` | 96 KB | dense binary, lower entropy than code: tables or initialised data |
| `0x108000`–`0x124000` | 112 KB | sparse records of small values with `00`/`FF` padding: config / register tables |
| `0x124000`–`0x134000` | 64 KB | plain ASCII debug, log and error strings (~1,200) |
| `0x134000`–`0x14C000` | 96 KB | mixed tables |
| `0x14C000`–`0x164000` | 96 KB | table data; parts look like packed bitmaps / glyphs |
| `0x164000`–`0x170000` | 48 KB | on-screen menu text, all languages, as glyph indices (below) |
| `0x170000`–`0x180000` | 64 KB | bit patterns that look like font glyphs, plus character-map tables |
| `0x180000`–`0x181560` | 5 KB | trailing data incl. templates `Dell xxxxxx`, `00.00000.000` |

The header's RAM range (`0x180000`–`0x209C79`) is far smaller than this blob,
so those fields do not simply describe where the whole AP sits. Unexplained.

### Menu text encoding (important for features a & b)

Menu/OSD strings are **not obfuscated**. They are stored as **glyph indices
into the OSD font**, not as ASCII, which is why `tools/strsearch.py` misses
them. Earlier notes described this as a "+45 additive cipher"; that is only
what the lowercase range happens to look like. The map, confirmed by encoding
known phrases and finding each exactly once in the image:

| Raw byte | Meaning |
|---|---|
| `0x00` | string terminator |
| `0x01` | space |
| `0x04`–`0x0B` | `'` `(` `)` `+` `,` `-` `.` `/` (ASCII order, `*` skipped) |
| `0x0C`–`0x15` | `0`–`9` (from `Dell S718QL`, `HDMI 1 (2.0)`, `16:9`) |
| `0x16` | `:` |
| `0x17`, `0x18`, `0x19` | probably `>`, `?`, `@` |
| `0x1A`–`0x33` | `A`–`Z` |
| `0x34`–`0x4D` | `a`–`z` |
| `0x4E`–`0x6D` | accented and typographic characters: `ç ñ … ¿ á é í ó ú « » É à â è ê ’ Ä ß ä ö ü` (individual codes in `tools/mst9u_strings.py`) |
| not yet mapped | `0x02`, `0x03`, a few more accents, and all CJK / other scripts (multi-byte sequences with `0xFF`) |

`tools/mst9u_strings.py` dumps everything: `menu_strings.tsv` (4,872
null-separated entries in `0x164000`–`0x170000`; 1,537 decode fully as Latin
text, the rest are other scripts or non-text records) and `ascii_strings.tsv`
(6,223 plain strings). Unknown glyph codes are written as `<xx>`.

The menu block is laid out language by language: the English top-level menu
starts at `0x164B60` (`Input Source`, `Brightness/Contrast`, … `Power
Settings`, `Keystone Settings`, `Bluetooth Settings`, `Information`), followed
by the same list in Spanish, French, German and then non-Latin languages.

The **Power Settings** submenu (English at `0x1675FC`) is: `Auto Shutdown`,
`Network` (`Standby On` / `Standby Off`), `Power Mode` (`Normal` / `Eco`),
`Power On Beep` (`On` / `Off`). These line up with the NXP MCU's EEPROM
settings (see nxp-mcu.md): which item maps to which byte is inferred, not
traced.

Located with this map: `Press Power Button to Turn Off Projector` at
`0x16C610`, `…please DO NOT unplg the power cord` at `0x16C651` (**the `unplg`
typo, feature b**, word at `0x16C65F`), `White Screen` at `0x16C6A7`,
`Language` at `0x164BB6`. Strings are null-terminated and packed back to back,
so `unplg` → `unplug` still needs one extra byte: there is no slack after the
word. Short non-text records (e.g. `D6 AC 55 00 7D 00`, which read like a
24-bit value plus a small number) sit between string groups; whether strings are found by pointer or by counting terminators is not
yet known and decides how a length change must be done.

The "+71 block" in earlier notes is not confirmed: the one block that scores
as text under +71 (`0x140000`) decodes to noise.

---

## 2. DDP FW — `Dell-s718ql-ddp-firmware-a30-2018-10-23.img` (TI DLPC4422) — identified

Raw ARM image (starts with an ARM exception-vector table:
`06 00 00 ea ...` = `b 0x18` then `ldr pc,[pc,#0x1f8]`×7). ~14 MB. Contains:
- At `0x20000` a structured table starting `67 45 23 01` with ASCII section tags
  (`CA`, `CO`, `PPA`, ...) — looks like a **flash sector / sequence directory**.
- DLP API markers: `$$APIVER_0x07010000_$$`, `$$CFGSEQ_0x44210019_$$`,
  `$$CFGSW_0x44210000_$$` (0x4422 = the DLPC4422 part number).
- `S718QL`, `DELL`, `DLP PROJECTOR`, `DLP WXGA`, firmware-version formatters.
- Subsystem strings: OSRAM/dynamic-black lamp ballast, TMP75 temperature,
  TCA8418 keypad, EEPROM I²C, and an on-chip **8051** (`8051: ... DDCRAM`).
- Fan/thermal state machine shared with the system side: `Cooling-Normal`,
  `Cooling-AB`, `Hi-attitude`, `Full-Speed`, `powerstate=off`.

No standard container/compression signatures found (binwalk clean), i.e. the
whole thing is a flat controller image with an internal directory. Flashed via
TI's GUI with "Complete Image Download / Skip Boot Loader Area 128KB / ASIC
Flash". Internals not yet mapped.

---

## 3. Network FW — `ACTUPGRADE.bin` (Actions Micro AM8250) — CONFIRMED (container)

Actions-Micro "ActionsFirmware" container, `UpdateVer:v1.27`. Parsed by
`tools/act_unpack.py`. 32-byte records from offset 0x40; the checksum record at
0x20 holds a **32-bit additive sum** over the body which we **reproduce exactly**
(`0x30cf215e`). Records:

| Record | Offset | Size | Meaning |
|---|---|---|---|
| ADECadfus, ADFUadfus, HWSChwsc, F648fwsc, EROMfwsc, F648mbrec, F648brec | 0x400.. | small | Actions ADFU boot/loader + ROM blobs |
| FIRM | 0x45e00 | 0x9e0000 | main firmware blob |
| rootfs | 0xa1ba00 | ~233 MB | **ext2** root filesystem (extracts) |
| user1 | 0xf31ba00 | 0xa00000 | **ext2** (web UI, configs) |
| bak | 0xfd1ba00 | 0x400000 | **ext2** (wifi/hostapd configs) |
| vram | 0x1011ba00 | 0xc200 | FAT (runtime state) |

**rootfs contents (confirmed):** busybox Linux (MIPS, `/lib/ld.so.1`),
`/etc/init.d/rcS`, EZCast app set under `/am7x/bin` (`manager.app`, `fui.app`,
`miracast.app`, `rs232_main.app`, `wol.app`, ...). Kernel `2.6.27.29`. See
[`feature-targets.md`](feature-targets.md) for the root-access details (c) and
the already-present `wol.app`/RS232 handlers relevant to (d).

Extract a filesystem image with `7z x part_rootfs.bin` (p7zip reads ext2).

---

## 4. Actiontec FW — `install.img` (Realtek RTD1295) — identified

A **GNU tar** (not a custom container). `config.txt` inside declares the install
plan: `version="SBDP1.05"`, `secure_boot=0`, empty `signature=""`, and a list of
partitions/images to write (`rootfs.ext4.bin` 700 MB, `etc.ext4.bin`,
kernels `emmc.uImage` (ARM64), dtb `Realtek,rtd-1295`, `bluecore.audio`,
bootloader.tar, teeUtility.tar, a GOLD rescue set, etc.).

- `etc.ext4.bin` extracts cleanly (p7zip): Wi-Fi calibration `cal-8822be`,
  `p2p_supplicant.conf`, ScreenBeam `aei/` data.
- `rootfs.ext4.bin` is a large ext4 with extents; the system p7zip (17.05)
  misidentifies it as ELF — needs a newer p7zip or a real `debugfs`/loopback
  mount to walk. Tracked in rebuilding doc.
- `customer.tar` holds the on-screen updater UI (`install_UI.exe`, PNGs,
  DroidSans.ttf).

`secure_boot=0` + empty signature is the encouraging part for custom code (c).

---

## 5. NXP MCU FW — `*.hex` (NXP LPC11U67, Cortex-M0) — identified

Intel HEX. Converted to flat binary in `firmware/unpacked/mcu/nxp_lpc11u67.bin`.
Populated regions: **`0x0000`–`0x1f00`** (a `BootCode` — strings `BootCode`,
`Update`, `Reset`), **`0x2000`–`0x9a00`** (the application; vector table at
0x2000 with SP `0x10001dc0`), and a tiny config word at `0x1ff00`. So the chip
runs a resident bootloader that can update the app region — relevant to how
risky a bad flash is.

Little plain text beyond the boot strings (it's bare-metal control code).
Flashed over RS232 @19200 with `PIC_NXP_Downloader.exe`. This is the **system
power/IR/thermal MCU** — primary suspect for feature **f** (unresponsive power
button) and a key player in **d** (remote power-on).

---

## 6. Formatter MCU FW — `*.isp` (Nuvoton M451, Cortex-M4) — identified

Nuvoton ISP project file. Header `[NU_SYS]` with config words, then an `APRM`
(APROM) blob at offset 0x400 containing the actual M451 code. Extracted guess in
`firmware/unpacked/mcu/nuvoton_m451_aprom_guess.bin`.

Strings confirm its role as **I²C glue / formatter coordinator**:
`GPIO_Set_Path_Switch=%d`, EEPROM read/write, `M054_Read/Write` (talking to the
NXP "M054"), `@PowerOn event from M054!!`, `DDP DownLoad Mode event from
M054!!`, `Power Off`, `FirmUpgradeDDP`, `UART_Cmd_End`, and the
`AMXB<-SDKClass=VideoProjector>...Model=NEBULA-P1>` AMX device-discovery beacon.
Config: part `M451RD3AE`, APROM 73728 B, config `FFFFFF7F / 0001F000`. Flashed
over RS232 with `NuvoISP.exe`. Source path `XGIMI_T1_M451_AppCode`.
