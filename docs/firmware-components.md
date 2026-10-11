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
| **tail resources** | `0x0a9cf9`–EOF | referenced by the TEL table; includes a JPEG at `0xb9d08` (the boot/no-signal logo bitmap, *inferred*) |
| **Trailer** | EOF-12 | `55 AA AB CD` magic; **CRC-32** (poly 0x04C11DB7, MSB-first, init 0, no final XOR) over everything before the last 4 bytes, stored **big-endian** |

Header u32s at 0x10000 (LE): `ap_flash_start=0x20080`, `ap_ram_start=0x180000`,
`ap_ram_end=0x209c79`, `ap_flash_end=0xa9cf9`, `0`, `0x1b41bbca` (unknown — a
second checksum/signature over the AP?), `0`, `0`, `filesize-4=0x167a28`,
`0x10000`.

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

Because it carries the image's own sizes, include it when trying to explain
the header field `0x1b41bbca`.

**Both integrity checks are reproduced** (CRC-16 `0x679f` and CRC-32
`0x2d384606` both recompute correctly). The one still-unexplained field is
`0x1b41bbca` in the header — likely an AP-region check; needs confirming before
repacking (see rebuilding doc).

**Strings of interest** (all in the decompressed AP): OSD menu text, `Dell
S718QL`, keystone (`dvOmnitek_*Keystone_Write` → an **Omnitek** warp block),
`api_CEC_ProcessCommand` / `monitor_ap/DRIVER/mapi_CEC.c` (CEC lives here),
`PowerOffSystem`, `enter ePM_POWER_DPMS`, logo-capture functions, and the USB
upgrade engine (`MW_UsbDownload_*`, `MERGE.BIN`, `Merge2.bin`).

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
| `0x0A` | line break within a message |
| `0x16` | `:` |
| `0x1A`–`0x33` | `A`–`Z` (ASCII − 39) |
| `0x34`–`0x4D` | `a`–`z` (ASCII − 45) |
| `0x5B`, `0x5D`, `0x6B`, … | accented letters (`é`, `ó`, `ä`, …) |
| not yet mapped | digits, remaining punctuation, CJK and other scripts (multi-byte sequences with `0xFF` appear in those sections) |

Located with this map: `Press Power Button to Turn Off Projector` at
`0x16C610`, `…please DO NOT unplg the power cord` at `0x16C651` (**the `unplg`
typo, feature b**, word at `0x16C65F`), `White Screen` at `0x16C6A7`,
`Language` at `0x164BB6`. Strings are null-terminated and packed back to back,
language after language (English, then Spanish, …), so `unplg` → `unplug`
still needs one extra byte: there is no slack after the word. Short records
that look like an index (`.. FC 55`, `.. FD 55`) sit just before the message
block; whether strings are found by pointer or by counting terminators is not
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
