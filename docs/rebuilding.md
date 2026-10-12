# Rebuilding the firmware images

How to reconstruct each image after modification. The golden rule: **a repacker
must round-trip an unmodified input to a byte-identical output first.** Until it
does, we don't understand the format well enough to trust a flash.

**Flashed on hardware so far:** the **Network (AM8250) `ACTUPGRADE.bin`** (booted
OK) and the **Scaler `MERGE.bin`** (features a + b, builds v1–v11, all applied) —
see §1 and §2.

Risk ordering (safest to scariest to experiment on):
**Network (AM8250) ≈ Actiontec (RTD1295) ≪ Nuvoton MCU ≈ NXP MCU ≪ DDP ≪ Scaler.**
The Linux SoCs have rescue/gold images in the package; the scaler's updater
lives *inside* the image you'd be replacing — but in practice `sboot`/`pm_8051`
stay byte-identical across our builds, so Download-Mode recovery has held without
an external SPI programmer (proven 2026-10-12). Keep that recovery path in mind,
but it is no longer the near-certain brick it was assumed to be.

---

## 1. Scaler — `MERGE.bin` — repacker WRITTEN + round-trip PROVEN + flashed on HW (features a + b live)

`tools/mst9u_merge.py` parses/verifies **and now repacks** it. Two modes:
- `roundtrip` — passthrough rebuild that REUSES the original deflate bytes and
  recomputes the framing. **Proven byte-identical** against the pristine
  MERGE.bin (all four checksums reproduce; 1,473,068 B match exactly). This is
  the keystone: it proves we can reproduce the brick-critical regions
  (pm_8051/sboot/ap_prefix/tail) and every framing field before any flash.
- `repack <MERGE.bin> <ap_decompressed.bin> <out>` — rebuild with a modified AP:
  re-DEFLATE + recompute all derived fields + fix up resource offsets (below).
  `region_diff()` reports exactly which regions changed (the diff-gate mitigation).

What the repacker does, step by step:

1. Modify the decompressed AP (`ap_decompressed.bin`) and/or tail resources.
2. Re-DEFLATE the AP as a **raw** stream (`zlib.compressobj(9, DEFLATED, -15)`).
   The on-device boot path inflates **standard raw DEFLATE** — confirmed:
   Python `zlib.decompressobj(-15)` inflates the shipped stream to our exact
   `ap_decompressed.bin`. Re-deflating the unmodified AP at level 9 is ~`0x86c3c`
   vs the original `0x86473` (**~+2 KB**) — note this means **any** re-deflated
   image differs from the vendor's even with no logical change, so a *patched*
   image can only be validated by re-checksum + region diff, never by comparing
   to the original. Mind the total flash budget.
3. **Fix up the TEL resource offsets (critical).** The 4 TEL records (RAM
   `0x18200e`, 11 B each: `[id:2][offset:4][size:4][flag:1]`, big-endian) store
   **absolute MERGE.bin file offsets** (used via the `0xC0000000` flash window),
   and the resources are packed contiguously right after `ap_flash_end`. Because
   re-deflation grows the stream and shifts the whole tail by `delta`, every tail
   offset must be bumped by `delta` or the loader reads the wrong bytes (broken
   logo/JPEG). `shift_tel_offsets()` does this in the uncompressed prefix, which
   the CRC-16 + header CRC-32 then re-cover. (Verified: the JPEG offset lands on a
   valid `FFD8` SOI after the shift.)
4. Rewrite the header u32s at `0x10000`: `ap_flash_end`, `ap_ram_end` (tracks the
   flash-region size), the CRC-32 field (step 6), and `filesize-4`.
5. Append u32 decompressed-size, then the **CRC-16/CCITT-FALSE** over
   `[0x20080 .. stream_end)`.
6. Recompute **header[5]** = CRC-32 (MSB-first, poly `0x04C11DB7`, init 0, no
   final XOR) over the **AP flash region** `[0x20080 .. ap_flash_end)`.
7. Recompute the **trailer** CRC-32 (same algorithm, stored big-endian) over
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

### Flashed on hardware — repack mechanics and code injection PROVEN (2026-10-11/12)

Patched scaler images have been flashed and run many times (features a and b,
builds v1–v11; see feature-a-status.md). That validates the whole pipeline on
real silicon: re-DEFLATE + TEL fixup + all four checksums produce an image the
on-device updater accepts and boots, AP-stream data patches land where intended,
and AEON code caves in the AP-prefix padding (`0x182048…`) execute.

For a while on 2026-10-12 builds after v1 appeared not to apply. That was the
**flash procedure being done wrong**, not the image — the correct procedure is
in "Build & flash a scaler patch" below and has worked every time since.

**The repacker output is VERIFIED sound (so the "build is broken" theories are
ruled out):** the current `dist/scaler/MERGE.bin` passes the **device's own
integrity gate** — CRC-32 (poly `0x4c11db7`) over the whole file gives residue
**`0x00000000`** (= pass), identical to stock. The **TEL fixup is verified
correct**: after the +~2 KB shift, every resource offset still resolves — the
JPEG (`id=0xc500`) moved `0xb9d08 → 0xba4e1` and still lands on `FF D8` (SOI),
the trailer resource on `55 AA`. And v1 was itself `delta≠0` (+1995 B over stock)
through the same code, and flashed.

**Brick / recovery note (precise):** `sboot`/`pm_8051` are kept byte-identical, so
Download-Mode recovery stays available by design. What's been *observed*: a
**stalled flash attempt was non-fatal** (power-cycling returned the projector to
the previous image), and re-flashing over a working patched image works
repeatedly. Recovering from an AP that does **not boot** has not been exercised
(no build has failed to boot), so treat that as expected-but-unproven.

**How the USB updater works (from the AP, `MW_UsbDownload_*` / `Flash` /
`FUN_ram_0021758b`):**
- **Trigger (official Dell procedure, from the setup-guide PDF):** USB in the
  Type-A port, projector OFF. Plug in power (do *not* press power to turn on). At
  **Standby**, press & hold **Power**; when the Power LED flashes Amber/White,
  press **Freeze** on the remote, then release both → Download Mode. LED sequence
  Amber → Power+Menu White → White → White-flashing; ~40 s; auto-returns to
  standby (white-flashing) on success. There is also a **boot-time auto-update**
  path (`FUN_ram_001f386c` checks a flag and calls the updater) — this is what
  "restarts itself" after a successful write.
- **File search:** the updater scans the **FAT root** for a file named
  **`MERGE.bin`** only. (`"Merge2.bin"`/`"AP.bin"` appear in log strings but are
  **not** real search targets — the only filename constant in the image is
  `MERGE.BIN`, so renaming to `AP.bin` is a dead end; there is no app-only image.)
- **Integrity gate:** before writing, the whole file is checked with **CRC-32**
  (poly `0x4c11db7`) — `"CRC32 Integrity Check pass"` / `"...failed"`.
- **Write:** a **sector-differential** compare/erase/write/verify over 64 KB
  sectors (`"Comparing image..."`, `"Erasing..."`, `"Writing..."`,
  `"Verifying..."`, `"Success."`) — only sectors that differ from what's in flash
  are rewritten. **No version / anti-rollback / same-version-skip gate anywhere.**
- The updater is **chatty on the scaler debug UART** — a serial capture shows
  exactly which stage runs (or `"No updated file in USB is found"`), and is the
  definitive diagnostic if a flash misbehaves.

### Build & flash a scaler patch — step by step

**Build** (`tools/build_scaler.py`): applies an assembled AEON cave + size-neutral
data patches to stock `MERGE.bin`, recomputes all framing (CRC-16, header CRC-32,
trailer CRC-32, `ap_flash_end`/`ap_ram_end`/`filesize`, TEL offsets), and
self-checks. Env `NOSTAMP=1` skips the on-screen version stamp (clean text).

```
# 1. write a cave listing: tools/aeon-ghidra/patches/<name>.lst
#    Each hook site (@0xADDR) = b.j to its cave + 2-byte nops (b.movi r0,0x0) to
#    fill whole displaced instructions. Caves start at @0x182048 (empty AP-prefix
#    padding), do the work, re-run the DISPLACED original instructions verbatim
#    and contiguously, then b.j back to just after the hook. Several hook sites
#    per listing are fine. Branch targets are absolute: assemble once, read the
#    emitted addresses from the .asm.txt, fix the targets, assemble again.
# 2. assemble it to bytes with the SLEIGH assembler (Docker):
docker run --rm -v "$PWD/firmware/unpacked/scaler":/in:ro \
  -v "$PWD/tools/aeon-ghidra/patches":/patches -v "$PWD/tools/aeon-ghidra/scripts":/opt/scripts:ro \
  aeon-ghidra /work aeon -import /in/ap_combined.bin -processor AEON:BE:32:R2 \
  -loader BinaryLoader -loader-baseAddr 0x180000 -scriptPath /opt/scripts \
  -postScript AeonAssemble.java /patches/<name>.lst /patches/<name>.asm.txt -deleteProject -overwrite
# 3. build the image (stock MERGE.bin, asm.txt, out, then any data-patch .txt files):
NOSTAMP=1 python3 tools/build_scaler.py \
  "firmware/extracted/.../Projector FW/MERGE.bin" \
  tools/aeon-ghidra/patches/<name>.asm.txt \
  dist/scaler/MERGE.bin \
  tools/aeon-ghidra/patches/feature-b-unplug.txt [more data patches ...]
```

**The current release build** (features a + b, no stamp; sha256 `918612e2…9135b`):

```
NOSTAMP=1 python3 tools/build_scaler.py \
  "firmware/extracted/Dell-s718ql-projector-firmware-m0r005-2018-11-30/Projector FW/MERGE.bin" \
  tools/aeon-ghidra/patches/feature-a-v11-allbg-black.asm.txt \
  dist/scaler/MERGE.bin \
  tools/aeon-ghidra/patches/feature-b-unplug.txt \
  tools/aeon-ghidra/patches/feature-a-v11-black-data.txt
```

`build_scaler.py` guards: every data patch has an `expect` value; each hook
site's overwritten bytes must reappear contiguously in the cave; the cave region
must be empty in stock; the repacked image must pass the container self-check.

**Verify before flashing** (cheap, catches everything we've hit):
- `mm._check(built)` passes (build does this).
- **Device CRC-32 gate**: residue of CRC-32 (poly `0x4c11db7`) over the whole file
  must be `0` — this is the exact integrity check the on-device updater runs.
- **Re-disassemble the built image** at the caves, hook sites and code-level data
  patches: extract prefix + inflated AP from the built `MERGE.bin` (via
  `mst9u_merge.parse`) to `firmware/unpacked/scaler/ap_combined_patched.bin`,
  import that with `-noanalysis`, and run `-postScript AeonDisasmAt.java <count>
  <addr>...`. Confirm branch targets resolve and the displaced instructions
  continue the hooked function. (Caveat: linear disasm can misalign in functions
  with indirect flow — cross-check the flow `.asm`, not just the `.c`. Code
  reached only through data pointers is absent from the flow `.asm`; read raw
  bytes.)

**Flash** (procedure now confirmed working — this was earlier mis-done):
1. Projector **OFF, power cord UNPLUGGED**. USB (FAT32) with `MERGE.bin` in the
   **root** in the Type-A port.
2. **Plug in power — do NOT press Power.** It sits at **Standby**.
3. Press & hold **Power**; when the Power LED flashes **Amber/White**, press
   **Freeze** on the remote, release both → Download Mode. ~40 s, LED White →
   White-flashing, then auto-returns to standby on success.
4. Power on normally to see the result. **The on-screen result (colour/stamp) and
   the USB sha are how you confirm which build ran** — the updater has no version
   gate and will flash whatever valid `MERGE.bin` it finds.

Mac USB helper: `cp dist/scaler/MERGE.bin /Volumes/FWUPDATE/MERGE.bin && dot_clean
-m /Volumes/FWUPDATE && rm -f /Volumes/FWUPDATE/._MERGE.bin && sync && diskutil
eject /Volumes/FWUPDATE` (strip the macOS `._*` AppleDouble so the embedded FAT
reader sees a clean root).

**Version stamp:** by default `build_scaler.py` writes a 7-hex build id into the
HDMI1 no-signal line (git short-hash when the tree is clean, else a content hash),
overwriting the glyphs `"switch input"` @`0x2f05be`. It **corrupts the on-screen
text** (it bleeds into the next string, "Se" from "Searching", because the
renderer draws the whole 284-byte block by length), so it is for diagnostic
builds only. Release builds use `NOSTAMP=1`; identify them by sha.

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

## 2. Network (AM8250) — `ACTUPGRADE.bin` — repacker WRITTEN + round-trip PROVEN; rooted image BUILT; **repack VALIDATED ON HARDWARE** (flashed, booted OK)

The cleanest target. `tools/act_unpack.py` splits it; `tools/act_repack.py`
rebuilds it. **Round-trip is byte-identical** (re-packing the pristine image with
no changes reproduces it exactly, SHA-256 `2cf39081…`, and the recomputed sum
equals the stored value). **A repacked `ACTUPGRADE.bin` was flashed and the unit
came back OK** (reported on-screen IP `10.0.50.100`, the static `eth0` from
`user.sh`) — so the container/additive-sum repack is proven on real hardware. The
first flash was the serial-root build; the dropbear-SSH build (port 22,
`root`/`projector`) was prepared next.

**The integrity field — fully pinned.** CHECKSUM record at file offset `0x20`
(`name[16]`, then `<4I` at `0x30`: `hdr_len`, `body_len`, `0`, `checksum`). The
checksum is a **32-bit additive sum of the little-endian u32 words over the body
`[hdr_len : hdr_len+body_len]` = `[0x40 : EOF]`, mod 2^32**. For M0R005:
`hdr_len=0x40`, `body_len=0x10127bc0`, stored sum **`0x30cf215e`** (reproduced).
The 4-byte checksum field (inside the 0x40-byte header) is not covered by itself.

`act_repack.py` is a *patch-the-original* repacker (not reassemble-from-parts),
so it preserves all inter-record padding and every untouched record verbatim:
```
act_repack.py <src.bin> <out.bin> [--replace NAME=PATH ...]   # NAME e.g. rootfs
act_repack.py --verify <container.bin>
```
A `--replace` file must exactly equal the record's slot size. It copies src→out,
splices the replacement at the record offset, recomputes the sum, rewrites `0x3c`.

**Filesystem editing.** `part_rootfs.bin` is **ext2** (1 KiB blocks). Edit a work
copy in Docker with `debugfs -w` (`rm` + `write` + `sif` to restore mode/uid/gid —
note the vendor files are **mode 0775, uid/gid 1023**, a quirk to preserve), then
`e2fsck -fn`, then `--replace rootfs=<work copy>`. Splicing + re-summing changes
only the rootfs region + the 4-byte sum (verified: a password-only edit touched
**78 bytes total** across the whole 269 MB image).

**Rooting status (feature c) — SERIAL + SSH, image BUILT.** Current
`dist/network/ACTUPGRADE.bin` (SHA-256 `d572c10a…`) delivers root TWO ways:
serial and network. Full build record + undo in `dist/network/MANIFEST.txt`.
(The earlier serial-only build `d79798f2…`, which merely blanked the root
password, is superseded by this one.)

- **Serial root (passwordless).** inittab's stock `::askfirst:-/bin/sh` execs
  `/bin/sh` directly — no login prompt, so it never consults `/etc/shadow` —
  giving passwordless root on the serial console. getty is commented out.
- **Network root (SSH, new).** dropbear 2022.83 on port 22, launched from rcS.

**Why a daemon had to be injected.** busybox v1.15.1 here was built **without
`telnetd`, `dropbear`, `inetd`, `httpd`, `tftp`, `wget` and even `nc`** —
verified against busybox's packed applet-name table, not just a string grep.
Networking applets are only `arp arping ifconfig netstat ping route rx udhcpc
udhcpd`. So **a config-only edit cannot open a root shell over the network**;
the rcS line `#/usr/sbin/telnetd` is a dead comment. (An earlier unauthenticated
bind-shell sketch, `overlays/network/tcpsh.c`, was abandoned in favour of a real
authenticated SSH server.)

**The injected daemon: static-musl dropbear.**
- **Build (static MUSL, not glibc):** static glibc breaks `getpwnam`/NSS so
  dropbear couldn't resolve users; **musl** reads `/etc/passwd`+`/etc/shadow`
  directly and works fully static. Toolchain `muslcc/x86_64:mipsel-linux-musl`
  (gcc 11.2.1; the host binaries are **i386**, so register the i386 qemu binfmt
  — `tonistiigi/binfmt --install 386,amd64` — before the cross-gcc will exec on
  an arm64 Mac). `./configure --host=mipsel-linux-musl CC=gcc --disable-zlib
  --disable-harden CFLAGS="-Os -fno-pie …" LDFLAGS="-no-pie …"` then
  `make PROGRAMS="dropbear dropbearkey scp" MULTI=1 STATIC=1`. The `--disable-harden`
  + `-no-pie` is **important**: the musl gcc defaults to `-pie`, which yields a
  static-**PIE** (`ET_DYN`) that is risky to load on the ancient 2.6.27 kernel;
  forcing non-PIE gives a classic `ET_EXEC` static binary (the safe format).
  Result: `dropbearmulti`, ELF32 MIPS-I LE o32, statically linked, 441336 B,
  SHA-256 `07234460…`. Smoke-tested under qemu-mipsel (usage prints; RSA+ECDSA
  host-key generation succeeds).
- **Install (via `debugfs -w`, image size unchanged, e2fsck clean):**
  `/usr/sbin/dropbearmulti` (0755) + `dropbear`/`dropbearkey` symlinks;
  `/etc/dropbear/` with **pre-generated** RSA+ECDSA host keys (0600) — the rootfs
  is read-only at runtime, so keys are generated at build time and read via `-r`,
  never written on first boot.
- **Auth:** dropbear rejects empty passwords, so the root password is set to a
  known md5crypt `$1$` hash (plaintext **`projector`**, documented in the
  manifest with "CHANGE THIS"). This enables SSH password auth and does **not**
  affect the passwordless serial `askfirst` shell.
- **Launch:** a non-fatal, backgrounded, `[ -x ]`-guarded block appended to
  `/etc/init.d/rcS` (after the `am7x_net.ko` insmod), pidfile in `/tmp` (tmpfs)
  so a read-only `/` is fine:
  `/usr/sbin/dropbearmulti dropbear -p 22 -P /tmp/dropbear.pid -r <rsa> -r <ecdsa> &`.
- **Verified:** output same size as source; additive sum self-consistent
  (`0x758b06fb`, VERIFY OK); whole-file diff vs pristine confined to the rootfs
  region `[0xa1ba00:0xf31ba00)` + the 4-byte sum field (ZERO bytes elsewhere —
  FIRM/user1/bak/vram byte-identical); the rootfs re-carved from the built image
  is `e2fsck -fn` clean with dropbear + keys + shadow + rcS line all present.
- **Residual risk:** musl/2.6.27 compat is only qemu-user smoke-tested, not run
  against a real 2.6.27 kernel (dropbear uses only classic syscalls and reads
  `/dev/urandom` directly, so it is likely fine; failure mode is benign — rcS
  backgrounds it). Default password is public until changed. Not flashed.

**OTA signature — RESOLVED (no image signing found).** The open question is
answered by static analysis of the update path:
- `am7x_ota_upgrade.ko` / `am7x_upgrade.ko` check only the container additive sum
  (`cal_checksum`, strings `ActionsFirmware`/`CHECKSUM`) plus zlib **crc32** on the
  *compressed FIRM blob* ("zip/unzip file checksum error") and an MBREC checksum —
  **no rsa/ecdsa/sha/md5/hmac/cert/public-key** anywhere. (`DecryptFlag`/`__key`/
  `remove HDCPKey` is HDCP-key handling, not firmware signing.)
- `fui.app` (userspace OTA) returns "Ota firmware is invalid" from that checksum;
  its `/etc/cert/*.pem` + `EncrypType=AES` + `ezcastPskDecrypt` are TLS/WiFi-WPA
  (the HTTPS *download*), **not** image signing. `manager.app`: no crypto strings.
- We only edited the rootfs ext2; FIRM and MBREC are byte-identical, so their
  CRCs still pass. **Conclusion: a correctly-summed image should be accepted.**
- **Residual unknown:** Dell's production flash is the USB/ADFU ROM path
  (`ADECadfus`/`ADFUadfus`); that boot-ROM protocol was not reverse-engineered,
  so whether the ROM adds a check is unconfirmed. Not flashed.

---

## 3. Actiontec (RTD1295) — `install.img` — repacker WRITTEN + round-trip BYTE-IDENTICAL (not flashed)

`tools/act_install_repack.py` rebuilds it. The container is **GNU tar + a
32-byte ASCII-hex MD5 trailer** (see firmware-components.md §4): the last 32
bytes are `md5(bytes[0:177213440])`, the sole integrity gate (`secure_boot=0`,
empty `signature`, no `verify=`/`checksum=` tokens, so the signature/`do_sha256`
path in `install_a` is off).

**Repack method — in-place splice (strongest guarantee).** Don't re-tar (GNU
metadata would drift). Instead splice the edited `rootfs.ext4.bin` into the
original tar at its exact data offset (`12366848`, length `112051200`) and
recompute only the MD5 trailer. Every other byte is then identical to the
source.

- **Round-trip PROVEN byte-identical:** splicing the *unmodified* rootfs back
  and recomputing the trailer reproduces the source exactly (SHA256
  `20de0ab4…`, `cmp` clean, MD5 trailer `7a28d01a…` reproduced). For a modified
  build the diff is provably confined to the ext4 region + the 32-byte trailer
  (verified: 0 differing bytes elsewhere; tar member list/order/sizes identical).
- **Edit the ext4 without resizing.** `mount -o loop,rw` (or `debugfs`) under
  Docker; the image file length stays `112051200` so the splice and all later
  tar headers remain valid. `e2fsck -fn` clean before and after. (System p7zip
  17.05 misreads the ext4 as ELF — use loopback/debugfs.)

**Rooting (feature c) — done, image built.** The rootfs is **BusyBox Linux, not
Android** (no adbd/props — see §4). Root was delivered the Linux way: add
`/etc/init.d/S98adbroot` (0755 root) which launches `telnetd -l /bin/sh -p 1023`
from `rcS` at boot — an always-on, unauthenticated **root shell** on TCP 1023
(the adbd-as-root analog), persistent on the flashed rootfs. The stock image is
already open too (empty root password; telnet :23; serial getty). Built artifact:
`dist/actiontec/install.img`, SHA256 `08a6bf33…`; full record in
`dist/actiontec/MANIFEST.txt`. (The injected init script itself — a leftover from
the early "this is Android, add adbd" assumption — is not kept under version
control; the manifest documents it verbatim.) **No dm-verity** (plain ext2/ext4,
remounted ro) so editing `/`
needs no hash-tree fixup; **GOLD rescue** images in the package make a bad flash
recoverable. Not yet flashed to hardware.

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

**Flashing from macOS/Linux** (no Windows downloader needed; untested until
the first real flash): USB-serial adapter on the DB9, then

```sh
uv run --with pyserial tools/nxp_flash.py IMAGE.hex --port /dev/cu.usbserial-XXXX --check   # standby; writes nothing
uv run --with pyserial tools/nxp_flash.py IMAGE.hex --port /dev/cu.usbserial-XXXX --flash   # in download mode
```

`--check` sends the app's `#00501 1` query and expects `P0`/`P1`: it proves
the adapter, wiring and baud rate before anything is erased.

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

## Versioning & anti-rollback (checked 2026-10-11)

All three flashable images carry **descriptive** version strings but enforce
**none** of them — every flash gate is an integrity checksum, so downgrade and
same-version re-install are both accepted (good for recovery and for our
same-identity patched images):

- **Scaler `MERGE.bin`** — build dates `Feb/Jun/Jul 2017`; model/version shown
  via `>>version=[%x]`. Flash gate = whole-file **CRC-32** (poly 0x4c11db7), no
  version compare. `FUN_ram_001f83e5` (refs `FW version (main)/(sub) changed!`)
  reads a stored *settings* stamp (`'R',0x92,0x18,0x01`) and, on mismatch,
  resets user settings to defaults — it is a settings-schema check, **not** a
  flash gate. Keeping stock version identity means saved settings survive.
- **Network `ACTUPGRADE.bin`** — `-M0R001`, `6.0.0.0`/`1.0.0.0` (EZCast web
  assets). Gate = 4-byte additive sum. No version check (validated: repack
  flashed & booted on hardware).
- **Actiontec `install.img`** — installer banner `VERSION:%s` / `DHC install_a
  (JH version)`. Gate = tar + MD5. `install_a` also checks a *boottable-format*
  version (`Wrong boottable version`) — a struct-format check, not a release
  check; unchanged by our rootfs-only edit.

Caveat: an in-menu updater (if used) might key a "already up to date / skip"
decision off the version string; since patches keep stock identity it could
decline. The raw USB download-mode / USB-file paths do not compare versions.
A display-only version bump is possible size-neutrally if ever wanted.

## Suggested order of work

1. **act_repack.py** + **a Nuvoton/NXP HEX round-trip** — easy, confidence-
   building, and NXP/Nuvoton are where the power/serial features live.
2. **Scaler data edits** — the container is fully solved (`0x1b41bbca` cracked,
   DEFLATE standard, all CRCs reproduced). Write `mst9u_merge.py`'s repacker and
   prove a byte-identical round-trip, then do the `unplg` fix. **DONE**, along
   with the no-signal colour (feature a) — both flashed and confirmed.
3. **Linux custom code** — pick AM8250 or RTD1295 for streaming once one of them
   is confirmed rebuildable and we've checked for extra on-device signature
   checks.
