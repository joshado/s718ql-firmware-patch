# Feature targets — where each wish lives, and how hard it is

Mapping the wishlist to the responsible processor, with findings so far and a
recommended approach. Difficulty is a rough estimate; "data edit" = no CPU
disassembly needed, "code edit" = needs the target's ISA.

| # | Goal | Lives in | Type | Difficulty |
|---|---|---|---|---|
| a | Change no-input-source background colour | Scaler (MST9U) | code hooks + data edits | **done** |
| b | Fix `unplg`→`unplug` shutdown typo | Scaler string table | data edit | **done** |
| c | Root on the two Linux images | AM8250 + RTD1295 | fs edit + repack | **low** |
| d | Remote power-on for Home Assistant | NXP MCU + AM8250 | code/daemon | high |
| e | HDMI-CEC working | Scaler (CEC) + others | code edit | high |
| e+| HDMI ARC audio to AVR | Scaler/formatter audio | code + HW | very high |
| f | Unresponsive power button | NXP MCU | code edit | medium–high |

---

## (b) The `unplg` typo — DONE (hardware-confirmed)

The cooling warning read *"…please DO NOT **unplg** the power cord…"*. OSD text
is stored as font glyph indices, not ASCII and not ciphered (`A`–`Z` =
`0x1A`–`0x33`, `a`–`z` = `0x34`–`0x4D`, space = `0x01`, terminator = `0x00`;
see firmware-components.md, "Menu text encoding"). There is no slack byte after
the word, so instead of growing the string it is reworded in place:
`"DO NOT unplg"` → `"NEVER unplug"` (both 12 glyphs) at RAM `0x2efe58`.
Patch: `tools/aeon-ghidra/patches/feature-b-unplug.txt`, folded into every
scaler build.

---

## (a) No-input-source background colour — DONE (hardware-confirmed 2026-10-12)

The searching and no-signal screens are now plain black, with the Dell logo
hidden and the no-signal text kept. Details:
[feature-a-osd-model.md](feature-a-osd-model.md) (how the screens are composed,
the patch table, side effects) and [feature-a-status.md](feature-a-status.md)
(summary + attempt log).

In brief:
- The steady blue was the display windows showing **video** with no input, not a
  colour register. Each window can instead show a per-window packed 3-3-2
  "no-signal colour"; `win_set_show_video` (`0x1ac05b`) selects which. The fix
  hooks that gate so the windows stay on the colour while `g_osd_mode` is 4 or
  `0x0b`, and forces every colour write (`win_set_nosig_color_332`, `0x1c6fe9`)
  to black.
- `0x90102f33` is the **frame colour**, visible only where no window covers the
  panel — the white surround of the searching screen. `osd_set_frame_color_rgb`
  (`0x1c70ec`) is patched to always write black.
- The no-signal Dell logo is **OSD text**: a 24×16 block of mono font glyphs
  (icon at `0x2da22e`, colour `0xc2` = white on Dell blue from the OSD palette
  at `0x2d2dc8`). Its colour bytes are patched to black-on-black. The
  blue-on-white JPEG (`images/jpeg_0b9d08.jpg`, TEL id `0xc500`) is a separate
  asset, used on the searching/splash screen.

Earlier conclusions in this section's history that turned out wrong (kept here
so nobody re-derives them): "the blue is the boot default of `0x90102f33`";
"there is no palette/CLUT"; "the logo is a 2bpp bitmap, index 3 = white";
"`0x2d9200` is a dispatch table and `0x1dbc84` the draw handler" (it is the menu
page table and a LoadFont callback); "only v1 ever flashed" (the flash procedure
was being done wrong at the time — rebuilding.md §1).

## (c) Root on the two Linux systems — genuinely easy

Both Linux SoCs are rebuildable from unsigned images, so we don't need to crack
anything — we **add our own access and reflash**.

**AM8250 (Network FW): DONE — rooted image built (not flashed).**
- `rootfs` has `/etc/passwd` with a single `root` account and a classic
  **MD5-crypt** hash in `/etc/shadow`. We blanked it (`root::14610:0:99999:7:::`)
  on repack — see `dist/network/MANIFEST.txt` and rebuilding §2. Built
  `dist/network/ACTUPGRADE.bin` (SHA-256 `d79798f2…`); only 78 bytes differ from
  stock (the sum + the shadow edit).
- **CORRECTION (was wrong):** busybox v1.15.1 here is built **WITHOUT `telnetd`,
  `dropbear`, `inetd`, `httpd`, `tftp`, `wget` or even `nc`** (verified against
  the packed applet-name table). The `#/usr/sbin/telnetd` line in rcS is a dead
  comment — no such binary exists. So "get a shell over the network" is **NOT**
  one uncomment away; **no stock applet can open a network socket shell.** Only
  the **serial** console shell is free (inittab's `::askfirst:-/bin/sh`, plus the
  now-blank password). Network root needs an **injected** daemon — a static MIPS
  bind-shell (`overlays/network/tcpsh.c`) is prepared but building it was blocked by the
  harness safety classifier; pending authorization. See rebuilding §2.
- OTA signature caveat **RESOLVED**: `am7x_ota_upgrade.ko`/`am7x_upgrade.ko`/
  `fui.app`/`manager.app` show the additive sum + zlib CRC32s only — no rsa/sha/
  md5/cert/public-key image signing (the `/etc/cert/*.pem` in fui.app are TLS/
  WiFi, not image signing). Residual unknown: the USB/ADFU boot-ROM path. See
  rebuilding §2.

**RTD1295 (Actiontec FW):** `secure_boot=0`, empty `signature`, Android-based,
GOLD rescue image present (recoverable). Same playbook once the ext4 opens with
a newer extractor. This is the **more powerful** SoC (quad A53, RTL8822BE Wi-Fi)
— the better home for "run our own streaming code."

**Recommendation:** prove the pipeline on the AM8250 first (simplest container —
now done: round-trip byte-identical + rooted image built; note telnetd is NOT
present so a network shell needs an injected daemon), then move streaming work to
the RTD1295 once we've confirmed its rebuild path. Check what each SoC's CPU/RAM
actually is before committing streaming workloads: AM8250 is a modest MIPS part
(256 MB DDR3 per `version.conf`), RTD1295 is the capable one (it's a TV-box
SoC with hardware H.264/H.265).

---

## (d) Remote power-on from Home Assistant — the headline feature

**Two sub-problems, both located.**

1. **"Ethernet dies in standby" (the actual blocker).** In standby the main SoCs
   (incl. the network stack that owns Ethernet) are powered down — so nothing
   answers the network. Dell's own listing says *"For EMEA models only, wake on
   LAN application required to power on from network standby mode."* The AM8250
   rootfs ships a **`wol.app`** in `/am7x/bin` — i.e. there IS a Wake-on-LAN
   path, presumably keeping *just* the PHY + a minimal listener alive. Getting
   this working (or extending it) is the most promising route and lands in the
   **AM8250**, which (per c) we can already modify.
2. **"Serial protocol is flaky" (your current workaround).** The RS232 control
   protocol is handled partly by `rs232_main.app`/`rs232/` on the AM8250 and
   partly by the **NXP MCU** (which owns the actual power rail) with the Nuvoton
   MCU relaying (`M054` events). The flakiness is likely in the NXP/Nuvoton UART
   handler framing/timing. Those are standard Cortex-M parts (fully
   disassemblable), so the protocol can be characterised exactly rather than
   guessed at.

**Recommended approach (matches your instinct):** instead of fighting RS232 from
outside, **run a small daemon on the AM8250** that (a) stays reachable via the
existing WoL/standby path and (b) issues the *internal* power-on command the way
the firmware itself does — then expose a clean MQTT/HTTP endpoint to Home
Assistant. Steps:
- Root the AM8250 (c).
- Reverse `wol.app` + `rs232_main.app` to learn how the network side signals
  power-on to the NXP MCU internally (which GPIO/UART/I²C message).
- Confirm whether the Ethernet PHY can be held up in standby (may need an NXP
  MCU tweak to leave that rail powered). This is the one part that might force an
  MCU code edit.
- Ship the HA-facing daemon in our repacked rootfs.

This is high-effort but every piece is in a processor we can already open.

**Update 2026-10-12:** the AM8250 already serves PJLink, a web UI, AMX,
Crestron and SNMP (`atop_main`), so no daemon needs injecting for control while
the projector is on. With the firmware's keep-network-in-standby option on, a Wake-on-LAN
packet powers the projector on from standby. A Home Assistant integration
that combines the two is in `ha/`; see [network-control.md](network-control.md).

**Update after disassembling the NXP MCU** ([nxp-mcu.md](nxp-mcu.md)): the MCU
does not unconditionally cut the network side. It has three standby flavours
selected by EEPROM settings — eco (standby rails off), normal (rails left on,
USART1 kept alive and able to wake the MCU) and WoL (the MCU bit-bangs MDIO to
arm magic-packet wake in the Ethernet PHY and watches its PME pin). So the
first steps are experiments, not patches: find which mode the unit is in, try a
magic packet, and check whether the AM8250 suspends itself. Two 4-byte patch
points (force normal standby; stop the MCU taking over the PHY) are documented
there if the MCU does turn out to be the blocker.

---

## (e) HDMI-CEC — stretch

**Located, not working today.** The scaler AP contains a real CEC implementation
(`api_CEC_ProcessCommand`, `monitor_ap/DRIVER/mapi_CEC.c`) but also
`[CEC] Invalid CEC Pad Setting!!` — suggesting CEC is present but mis-configured
/ the CEC line isn't wired to an active pad, or it's disabled in this build. The
network side also has `websetting_get/set_hdmi_cec`, so there's a config toggle.

**Approach:** first find out *why* it's inert — is it the "Invalid CEC Pad
Setting" (a GPIO/pad-mux config value we can fix as a data edit), or is the CEC
pin simply not routed on this board (hardware, dead end)? Check the pad/pinmux
config near that string and whether the `hdmi_cec` websetting actually reaches
the scaler. Logic changes would need the MST9U ISA. Medium-to-high, and partly
gated on a hardware question.

## (e+) HDMI ARC to the AVR — stretch-on-stretch

Very high effort / possibly infeasible. ARC needs the HDMI port to physically
support it (dedicated ARC pin + CEC) *and* firmware audio routing to send stream
audio back out over HDMI. No ARC/eARC strings were found in the scaler or DDP
images; only `websetting` CEC hooks. Realistically blocked on (e) working first
and on the board actually wiring ARC. Park until (e) is resolved.

---

## (f) Unresponsive power button

**Lives in the NXP MCU** (it owns the keypad/IR + power sequence). The symptom
(multiple presses needed to turn on) points at either debounce/edge handling or
a state-machine race in the power-on path — e.g. the MCU ignoring presses while
in a transitional standby/cooling state. The Nuvoton side's
`@PowerOn event from M054!!` shows the NXP MCU ("M054") is the press originator.

**Approach:** disassemble the NXP LPC11U67 app region (standard Cortex-M0 —
tractable in Ghidra/radare2), find the keypad/power-button GPIO ISR and the
power state machine, and look at the debounce + the guard conditions on entering
power-on. This is the most self-contained "real bug fix" in the list once we can
build/flash the NXP HEX (rebuilding §5), and the resident BootCode makes flashing
recoverable.

---

## Cross-cutting next steps

1. ~~Write `tools/mst9u_strcipher.py` (additive-shift OSD text)~~ **DONE/revised**
   — the OSD text is glyph-index encoded, not a cipher; `tools/mst9u_strings.py`
   decodes it. (b) was done as a same-length reword, so no string-table
   relocation was needed.
2. ~~Solve the scaler header field `0x1b41bbca`~~ **DONE** — it is CRC-32-MSB
   over `[0x20080..0xa9cf9)` (see rebuilding.md). Scaler repack is unblocked.
3. ~~Write the `MERGE.bin` repacker~~ **DONE** — `mst9u_merge.py` +
   `build_scaler.py`; features a and b are flashed and confirmed.
4. ~~Write `tools/act_repack.py`~~ **DONE** — byte-identical round-trip proven
   and a rooted AM8250 image built (see (c); flash status in rebuilding.md §2).
5. ~~Load the NXP image into Ghidra~~ **DONE** — NXP fully disassembled and
   decompiled (`decompiled/nxp_app.c`, `docs/nxp-mcu.md`). Nuvoton M451 still to
   do (also stock Cortex-M) for (d)/(f).
6. Get `rootfs.ext4.bin` (RTD1295) open with a current extractor → unblocks the
   streaming home for (c).
