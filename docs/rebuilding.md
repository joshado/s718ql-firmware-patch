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

**CPU note:** none of the above requires disassembling the MStar AP core —
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

## 5. NXP LPC11U67 — `.hex` — repack PLAUSIBLE

Standard Intel HEX of a Cortex-M0 image with a resident **BootCode** at 0x0 and
app at 0x2000. Rebuild = modify the app region, regenerate the HEX (keep the
0x1ff00 config word). Standard ARM, fully analysable. The resident bootloader
means the app can be reflashed over RS232 and is the recovery path — **do not
overwrite the 0x0–0x2000 BootCode region.** This is the right place to analyse
features d/f at the source level.

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
