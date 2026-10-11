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

## 1. Scaler — `MERGE.bin` — repacker DESIGNED, not yet written

We can fully parse and checksum-verify it (`tools/mst9u_merge.py`). To rebuild:

1. Modify the decompressed AP (`ap_decompressed.bin`) and/or tail resources.
2. Re-DEFLATE the AP as a **raw** stream (`zlib, wbits=-15`). ⚠️ The size must
   fit the flash budget; MStar's original compressor isn't zlib, so the stream
   bytes won't match even for identical input — that's fine as long as the
   on-device loader just inflates it, **but** it changes `ap_flash_end` and both
   trailing CRCs, so those must be recomputed.
3. Rewrite the header u32s (`ap_flash_end`, and the unknown `0x1b41bbca` field).
4. Append u32 decompressed-size + recompute **CRC-16/CCITT-FALSE** over
   `[0x20080 .. stream_end)`.
5. Recompute the final **CRC-32** (MSB-first, poly 0x04C11DB7, init 0, BE) over
   everything but the last 4 bytes.

**Blocking unknown:** header field `0x1b41bbca`. It isn't the CRC-16 or the
CRC-32. Hypothesis: a sum/CRC over the AP RAM image or the decompressed AP.
*Next step:* brute-force common checksums of `ap_decompressed.bin` and of the
compressed region against `0x1b41bbca` before trusting any repack. If it's an
unverified-by-the-loader field we can ignore it, but we must prove the loader
ignores it (via sboot disassembly) first.

**For the easiest wins (features a/b — strings only):** if the edit is
length-preserving we can patch `ap_decompressed.bin` in place, re-DEFLATE, and
only the two CRCs + `ap_flash_end` change. That path avoids needing to fully
understand `0x1b41bbca` *if* it turns out the loader skips it — otherwise we
solve it first. A length-changing edit (like `unplg`→`unplug`) additionally
needs either slack bytes after the word or a rebuilt string table.

**CPU note:** none of the above requires disassembling the MStar AP core. If we
do need it (logic changes), the AEON R2 Ghidra module `eb4x/ghidra-plugin-aeon`
covers this core (tested on MST9U firmware); load as `AEON:LE:32:R2` at base
`0x180000`. See firmware-components.md, "Tooling for this core". Originally:
none of the above requires disassembling the MStar AP core —
string/table edits are data edits. Logic changes (CEC behaviour, power) *do*
need the ISA, which is the hard, unsolved part.

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
2. **Scaler string table** — solve `0x1b41bbca`, then do the `unplg` fix and the
   no-signal colour change as data-only edits.
3. **Linux custom code** — pick AM8250 or RTD1295 for streaming once one of them
   is confirmed rebuildable and we've checked for extra on-device signature
   checks.
