# Rebuilding the firmware images

How to reconstruct each image after modification. The golden rule: **a repacker
must round-trip an unmodified input to a byte-identical output first.** Until it
does, we don't understand the format well enough to trust a flash. None of the
repackers below have been flashed to hardware yet.

Risk ordering (safest to scariest to experiment on):
**Network (AM8250) ≈ Actiontec (RTD1295) ≪ Nuvoton MCU ≈ NXP MCU ≪ DDP ≪ Scaler.**
The Linux SoCs have rescue/gold images in the package; the scaler's updater
lives *inside* the image you'd be replacing, so a bad scaler flash likely needs
an external SPI programmer to recover.

---

## 1. Scaler — `MERGE.bin` — repacker FULLY SPECIFIED (all gates solved)

We can parse and checksum-verify it (`tools/mst9u_merge.py`), and every piece
needed to rebuild is now understood (verified in-session; repacker still to be
written and flash-tested).

To rebuild:

1. Modify the decompressed AP (`ap_decompressed.bin`) and/or tail resources.
2. Re-DEFLATE the AP as a **raw** stream (`zlib.compressobj(9, DEFLATED, -15)`).
   The on-device boot path inflates **standard raw DEFLATE** — confirmed:
   Python `zlib.decompressobj(-15)` inflates the shipped stream to our exact
   `ap_decompressed.bin`. Re-deflating the unmodified AP at level 9 is `0x86c33`
   vs the original `0x86473` (**+1,984 bytes**), so the size delta from an edit
   is small; mind the total flash budget.
3. Rewrite the header u32s at `0x10000`: `ap_flash_end` and the CRC-32 field
   (below), plus the `filesize-4` field.
4. Append u32 decompressed-size, then the **CRC-16/CCITT-FALSE** over
   `[0x20080 .. stream_end)`.
5. Recompute **header[5]** = CRC-32 (MSB-first, poly `0x04C11DB7`, init 0, no
   final XOR) over the **AP flash region** `[0x20080 .. ap_flash_end)`.
6. Recompute the **trailer** CRC-32 (same algorithm, stored big-endian) over
   everything but the last 4 bytes.

**The former "blocking unknown" `0x1b41bbca` is SOLVED.** It is the CRC-32
(MSB-first, poly `0x04C11DB7`, init 0, no XOR) over `[0x20080 .. 0xa9cf9)` — the
same CRC family as the trailer, over the AP flash payload (prefix + deflate
stream + size + CRC-16). Reproduced exactly. So there are **no remaining
unknowns** in the container; a repacker can round-trip and re-sign correctly.

**For string/table edits (features a/b):** length-preserving edits are a clean
data patch (edit `ap_decompressed.bin`, re-DEFLATE, fix the three checksums +
`ap_flash_end`). A length-changing edit (`unplg`→`unplug`) needs slack or a
rebuilt string/index table; the OSD text is a packed glyph-index blob (see
firmware-components.md).

**Code edits are now mechanically possible too.** We have a working
disassembler **and assembler** for the AEON core (`tools/aeon-ghidra/`): Ghidra's
SLEIGH assembler round-trips AEON byte-for-byte (`b.movi r3,0x0`->`9860`,
`b.ori r7,r0,0x70`->`50e070`, `b.addi r1,r1,-0x8`->`9c38`). So in-place
instruction patches (flip a branch, change an immediate, redirect a call) can be
assembled exactly. **Caveat:** there is no public C **compiler** for AEON, so
"compile new C and inject it" is not available — new logic must be hand-written
AEON assembly (fine for small hooks, impractical for large features). Free space
for a hook exists past the image (BSS) or in padding, but a hook that grows the
image still goes through the re-DEFLATE + re-checksum path above.

**Not yet proven on hardware.** The repacker is specified but unwritten; nothing
has been flashed. Flashing risk is the highest of all components: the USB
updater lives *inside* the image being replaced, so a bad flash likely bricks
the scaler until reflashed with an **external SPI programmer**. Establish that
SPI recovery path (identify the flash chip, wire up a CH341A/flashrom) **before**
the first scaler flash.

**Recovery nuance (from the sboot RE, `decompiled/sboot.c`):** `sboot` and the PM
core are **8051** programs (Keil C51), *separate CPUs from the AEON AP*. They boot
first, own the SPI-NOR driver, and expose a **UART debug console with an RX
command ring** — so a corrupt AP flash should **not** brick the 8051 side, which
keeps running and talking. That makes the 8051 UART console the realistic
"recover without an SPI programmer" lever — **if** its monitor can drive a
flash erase/write (present as SPI opcodes incl. `0xd8` 64K-erase, but not proven
reachable from the console; needs hardware). sboot being 8051 also means a
safety-net patch to it uses a standard, available 8051 toolchain (no AEON-compiler
blocker) — but it is the always-on core, so a bad sboot flash is higher-stakes
than a bad AP. Still **UNCERTAIN**: whether the sboot CRC (`Check CRC Result_H/L`)
actually *gates* the AEON release and what it does on mismatch — needs banked-code
tracing or a serial console.

---

## 2. Network (AM8250) — `ACTUPGRADE.bin` — repacker STRAIGHTFORWARD

The cleanest target. `tools/act_unpack.py` splits it; the only integrity field
is the 32-bit additive sum at offset 0x20, which we reproduce. To rebuild:

1. Edit a filesystem (e.g. rootfs), rebuild it as **ext2** at the original size
   (`mke2fs -t ext2`, or edit in place via loopback / `debugfs`).
2. Splice the new partition back at its record offset.
3. Recompute the body sum and write it to the checksum record.

A full `act_repack.py` is a small amount of work and should round-trip trivially
because the container is just concatenated records + a sum. **To be written.**
Open question: whether the on-device OTA (`am7x_ota_upgrade.ko`) does any
*additional* signature check beyond this sum — must verify before flashing.

---

## 3. Actiontec (RTD1295) — `install.img` — repacker STRAIGHTFORWARD (once ext4 opens)

It's a GNU tar driven by `config.txt`, `secure_boot=0`, empty signature. To
rebuild: untar, modify `rootfs.ext4.bin` (ext4), re-tar in the same order.
**Blocker:** the system p7zip 17.05 can't open `rootfs.ext4.bin` (calls it ELF).
Fix by using a newer extractor or a Linux loopback mount / `debugfs` to edit the
ext4 directly; no format reverse-engineering needed. The presence of GOLD rescue
images means a bad flash is recoverable, which makes this a good first
custom-code experiment.

---

## 4. Nuvoton M451 — `.isp` — repack PLAUSIBLE

The `.isp` is `[NU_SYS]` config + `APRM` blob. Rebuild = replace the APROM blob
and fix the ISP's internal length/checksum fields (format of the `.isp` wrapper
needs confirming — compare several Nuvoton ISP files, or drive `NuvoISP.exe`
with a known APROM to see what it writes). The M451 is standard Cortex-M4, so
the *code* is fully analysable in Ghidra/radare2 (unlike the scaler). Flash is
reversible over RS232 as long as the bootloader/LDROM stays intact (don't touch
the config bits / LDROM).

---

## 5. NXP LPC11U67 — `.hex` — PATCHABLE; recovery path understood (untested)

Standard Intel HEX of a Cortex-M0 image with a resident **BootCode** at 0x0 and
app at 0x2000. The app is fully disassembled and hand-decompiled
([nxp-mcu.md](nxp-mcu.md), `decompiled/nxp_app.c`). Everything below is from
static analysis; nothing has been flashed.

**Why a bad app flash should be recoverable** (details in nxp-mcu.md,
"BootCode, recovery and debug access"):

- Download mode is entered by the BootCode itself (hold Power while applying
  AC), before the app runs, so a hung or crashing app can still be replaced.
- The BootCode's serial updater only ever erases `0x2000`–`0x1ffff`; it cannot
  erase itself.
- An "app valid" byte at `0x1ff00` is written last; without it the BootCode
  erases the app area and waits for an update.
- The jump-to-app path checks only that the reset vector is in range, and the
  download protocol has no checksum either. A patched image will not be
  rejected — and a corrupted transfer will not be detected, so a flasher
  cannot confirm what was written.
- Code read protection is off, so SWD and NXP's mask-ROM ISP (P0_1 low at
  reset) are available as deeper fallbacks **if** those pins can be reached on
  the board. Not checked.

**What is still dangerous:** the MCU sequences supply rails, fans and laser
enables. A wrong patch can mis-sequence real hardware even though the MCU
itself stays recoverable, and the BootCode raises some rails in update mode
too.

**Do not** program the `.hex` verbatim with an SWD tool: its vector checksum
slot at `0x1c` is zero, so its BootCode region is not what a working chip
holds. Keep to the app region, and **do not overwrite `0x0`–`0x2000`.**

**How to build a modified image — recommended: patch + hooks.** Keep the
original binary. Apply small byte patches (the candidates are tabulated in
nxp-mcu.md), and put any new logic in C compiled with `arm-none-eabi-gcc`
(`-mcpu=cortex-m0 -mthumb`), linked into the empty flash above `0x9a00` and
reached by redirecting an existing `bl`. Then merge back into a `.hex` keeping
the `0x1ff00` config record. Only the code we wrote is new. Still to write:
the linker script and the merge script. The flasher exists:
`tools/nxp_flash.py` speaks the BootCode's block protocol (documented in
nxp-mcu.md, "Serial download protocol"); it is untested on hardware.

**Not recommended: recompiling `decompiled/nxp_app.c`.** It is hand-decompiled
and unverified, and a different compiler emits different code, so there is
nothing to diff against. The way to make whole-program recompilation
trustworthy is a *matching* decompilation: the original looks like an
unoptimised Keil ARMCC build, so compiling function by function with the same
compiler and comparing bytes would prove equivalence. The app is ~31 KB, just
under the 32 KB limit of Keil's free edition (Windows only). Large effort.

**Debugging:** the app has no log output at all. Visibility comes from SWD (if
reachable), from code we add (e.g. a trace routine on a spare UART), or from
running the image in an emulator with stubbed peripherals before flashing.

---

## 6. DDP (DLPC4422) — `.img` — NOT YET UNDERSTOOD

Flat controller image with an internal section directory at 0x20000 and an
on-chip 8051. TI's GUI does "Complete Image Download, Skip Boot Loader Area
128KB". No repack attempted; this is the least-explored and (after the scaler)
the riskiest. Not needed for any current feature target, so parked.

---

## Suggested order of work

1. **act_repack.py** + **a Nuvoton/NXP HEX round-trip** — easy, confidence-
   building, and NXP/Nuvoton are where the power/serial features live.
2. **Scaler data edits** — the container is fully solved (`0x1b41bbca` cracked,
   DEFLATE standard, all CRCs reproduced). Write `mst9u_merge.py`'s repacker and
   prove a byte-identical round-trip, then do the `unplg` fix. The no-signal
   colour needs the renderer located first (string-less; data-flow trace — see
   feature-targets.md (a)).
3. **Linux custom code** — pick AM8250 or RTD1295 for streaming once one of them
   is confirmed rebuildable and we've checked for extra on-device signature
   checks.
