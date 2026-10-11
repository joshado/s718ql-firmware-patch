# Feature targets — where each wish lives, and how hard it is

Mapping the wishlist to the responsible processor, with findings so far and a
recommended approach. Difficulty is a rough estimate; "data edit" = no CPU
disassembly needed, "code edit" = needs the target's ISA.

| # | Goal | Lives in | Type | Difficulty |
|---|---|---|---|---|
| a | Change no-input-source background colour | Scaler (MST9U) | locate (code trace) + data/code edit | medium–high |
| b | Fix `unplg`→`unplug` shutdown typo | Scaler string table | data edit | low–medium |
| c | Root on the two Linux images | AM8250 + RTD1295 | fs edit + repack | **low** |
| d | Remote power-on for Home Assistant | NXP MCU + AM8250 | code/daemon | high |
| e | HDMI-CEC working | Scaler (CEC) + others | code edit | high |
| e+| HDMI ARC audio to AVR | Scaler/formatter audio | code + HW | very high |
| f | Unresponsive power button | NXP MCU | code edit | medium–high |

---

## (b) The `unplg` typo — most tractable; start here

> **Correction:** the text is not ciphered. It is stored as OSD font glyph
> indices (`A`–`Z` = `0x1A`–`0x33`, `a`–`z` = `0x34`–`0x4D`, space = `0x01`,
> terminator = `0x00`); see firmware-components.md, "Menu text encoding". The
> "+45 shift" below is just the lowercase range of that map. The word is at
> `0x16C65F` in `ap_decompressed.bin` and there is no slack byte after it.

**Confirmed located.** The OSD/message strings in the scaler AP are stored with
a per-block **additive character cipher** (not ASCII). The main multilingual
message block decodes with a **+45** shift. At `ap_decompressed.bin` offset
`~0x16c65f` the decoded text reads:

> *"...please DO NOT **unplg** the power cord while the projector fan is still
> running..."*

So the fix is: locate the ciphered bytes for `unplg`, and insert the missing
`u`. `unplug` is one character longer than `unplg`, so this is **not** a pure
in-place byte swap:
- **If** there's a null/separator with slack after the word, we shift the
  following byte and re-encipher — a clean data edit.
- **If not**, we rebuild that string's region (and any length/offset table that
  points at it — TBD whether the TEL table or an OSD index references string
  offsets).

Then re-DEFLATE + recompute CRCs (see [rebuilding.md](rebuilding.md) §1).
`tools/` needs a small `mst9u_strcipher.py` to encode/decode a given shift and a
locator for the string table — next concrete task.

**Note:** because the strings are length-sensitive, verify whether the renderer
uses explicit lengths or null-termination before editing. The decoded dump
shows null separators between messages, which is promising.

---

## (a) No-input-source background colour

**Status: subsystems identified by strings; mechanism NOT confirmed in code;
the exact colour value NOT found.** Be careful reading the earlier optimistic
notes — the following is what is actually established vs inferred.

*Established:*
- The Dell logo is a 2-tone JPEG (82% white bg, 18% Dell blue ~RGB 20,135,199)
  stored as TEL resource type `0xc5` at `MERGE.bin 0xb9d08`, size `0x5110`. Its
  resource record holds only type/offset/size — **no colour or palette data**.
- On screen the logo is white on a blue field (user-reported) — a figure-ground
  + colour swap that **no per-pixel RGB op reproduces** (negate→orange-on-black,
  R/B swap→orange-on-white).
- Strings exist for a "Frame Color"/free-run background (`[msScalerAP] Enable
  Frame Color`, `0x2aacf9`) and a logo/mute subsystem (`Mute BMP Logo!`
  `0x2b013e`, `initScanLogoCapture`/`setLogoCapture`, `USER_PREF_SYS_DRAW_BMP_
  LOGO`/`LOGOCAPTURE`/`LOGOASPECT`). The logo **preference** string is
  referenced by `FUN_ram_001a93ce`; the colour-temp defaults
  (`DefCool_RedColor` …) are referenced by several PQ functions (picture gains,
  not the background).

*Inferred, NOT verified in code:* that the logo is drawn as a mask/key with the
background (blue) and foreground (white) as separate colour parameters. It is
the only explanation consistent with the evidence above, but the actual drawing
code has not been read.

*Coverage pass done; renderer is string-less.* After exhaustive disassembly
(`DisassembleCodeRegion.java`, 35%->full, 2,690 functions) the ref search does
find `Mute BMP Logo!` — but in `FUN_ram_001c7c3d`, a **debug-dump routine**: a
repeating `load state field -> DbgPrint(label, value)` (printer at `0x1c59a3`)
that prints `Mute BMP Logo!`, logo and other state with labels. It is **not** the
renderer. `Enable Frame Color` (`0x2aacf9`) is not constructed anywhere in the
(now full) code — it is dead or reached via a pointer table. So the actual
no-signal renderer / colour fill has **no debug string to anchor on**; string
location cannot reach it.

*Trace so far (via the OSD message path).* Decompiled the engine and wrappers:
- `0x219504` is the glyph **renderer** (7 params; `param_2` = glyph string,
  `param_3` = length). 8 call sites.
- `0x1e9d6d` is a **show-by-id helper**: it copies an **ASCII** string through
  `FUN_ram_001e9942` (ASCII→glyph transform) and renders it. 8 callers.
- Wrappers `0x1e9de9` / `0x1e9f2d` / `0x1ea0c7` pick a message **id**
  (`0xde`, `0xf5`/`0xd0`, `0xf0`/`0xc2`, `0xe8`) by input **source**
  (`DAT_ram_0030f6d8`) and **state** (`DAT_ram_00307b98`), rendering ASCII
  labels from the `0x2b28xx` region.

*Where it's blocked.* Two walls, both from the analysis, not the chip:
- Those source-select wrappers have **no static callers** (no `b.jal`/`b.j`, not
  in any findable function-pointer table) — the no-signal screen is reached by
  **indirect dispatch**, which static analysis can't resolve here. The
  blind-sweep coverage also gives unreliable function boundaries, so the call
  graph for indirect/table-driven paths is incomplete.
- The pre-encoded no-signal **glyph** string (`0x2ecd94`) is **not referenced by
  address** and its per-language table isn't statically locatable (computed at
  runtime from base+language+screen-id).

**NO-SIGNAL SCREEN FULLY RESOLVED — it is a TWO-LAYER COMPOSITE** (confirmed by
four independent agents + spot-checks; the "Where it's blocked" indirect-dispatch
wall noted above is now cleared — the mode-4 renderer is `osd_nosig_draw`
`0x1dbc84`, reached via the OSD dispatch table `0x2d9200`, stride `0x1c`, slot
`+0x14`).
There is **no colour inversion** anywhere; the premise that the stored Dell JPEG
is recoloured was a red herring. The screen is built from two separate hardware
layers:

- **Layer 1 — the BLUE background:** the scaler's **free-run frame generator**,
  register **`0x90102f33`** (RGB888: `f33`=R, `f34`=G, `f35`=B; a 24-bit write via
  `scaler_reg_write24` puts `V = R | G<<8 | B<<16`). This is NOT part of the OSD.
- **Layer 2 — the WHITE Dell logo + "No signal"/"Press Source" text:** the mode-4
  **OSD overlay** (`g_osd_mode==0x04`), drawn by `osd_nosig_draw` (`0x1dbc84`):
  it streams two **2-bits-per-pixel indexed** OSD-GRAM bitmaps via
  `osd_gram_write_rle` (`0x21c925`, GRAM ports `0x101c66/67/68`, 12-bit RLE codes
  = six 2-bit pixels each; expander `0x21c727`) and the text via
  `osd_draw_current_screen_text` (`0x1e82ea`). **Index 0 = transparent** (the blue
  Layer 1 shows through); **index 3 = foreground = white.** There is **no RGB
  CLUT/palette** — the 4 index levels map to colour by a fixed OSD default, not a
  per-screen table (the CLUT hypothesis was tested and disproven).

So the **Dell logo on the no-signal screen is a monochrome OSD bitmap**
(`osd_nosig_logo_bmp` = `DAT_ram_002a6c92`, file off `0x126c92`; a second icon
`DAT_ram_002a74d8` at GRAM index `0xcf`), **NOT** the JPEG. The blue-on-white
JPEG (`images/jpeg_0b9d08.jpg`, B_ID `0xc500`) is a *separate* asset for the
**boot/menu splash** (mode 7, `FUN_ram_002027b0` → shown unmodified on a white
free-run background). The two assets simply have opposite colour schemes — that
is the whole of the "inversion."

**Where the blue comes from = the power-on/boot DEFAULT of `0x90102f33`.** Across
the entire AP, every literal written to that register is `0xffffff` (white, the
"searching"/ShowDiag path `osd_set_frame_color` `0x1ff83c`) or the burn-in cycle
table (`g_nosig_bg_colors` `0x2cf6e4`, mode `0x0d` only); **no code ever writes
blue.** The normal video→no-signal path (`osd_no_input_handler` `0x1e73fe` line
~76487) sets mode 4 with **no colour write**, leaving the register at its boot
default (≈ `0x00/0x00/0xFF`). The register address is built at runtime
(`movhi 0x9010`/`ori 0x2f33`) and never appears as a word literal, so there is no
(addr,value) init table for it in the AP — the default is set **outside** this
image (HW reset default, or `sboot.bin`/`pm_8051.bin` init). *(The former claim
"mode-4 never touches the register" was imprecise: one menu→no-signal branch,
line 76483, does write WHITE `osd_set_bg_color_332(0,0xff)` — but only when
signal is lost with a menu open, not the normal case.)*

**To change the blue — three options, best first:**
1. **Code patch (robust, universal — recommended).** Inject one colour write at
   the top of `osd_nosig_draw` (`0x1dbc84`, file off `0x5dc84`), which runs on
   every no-signal draw: `scaler_reg_write24(0x90102f33, R | G<<8 | B<<16)` (e.g.
   red = `0x0000FF`, green = `0x00FF00`, original blue = `0xFF0000`) or
   `osd_set_bg_color_332(0, <packed-3-3-2>)` (`0xe0`=red, `0x1c`=green, `0x03`=blue,
   `0x00`=black, `0xff`=white). Needs AEON code injection + repack (both solved).
2. **Limited data edit (menu path only).** Change the `0xff` immediate in
   `osd_set_bg_color_332(0,0xff)` at line 76483 to a packed-3-3-2 value — a pure
   data edit, but only affects the "signal lost with menu open" case, so it is not
   a complete fix on its own.
3. **Patch the boot default (cleanest if found, not located).** The write that
   makes the default blue is outside the AP — candidates: `sboot.bin`,
   `pm_8051.bin`, or a HW reg-init list. Not yet found.

*Verify empirically:* read `0x90102f33` over the serial/debug console at no-signal
before and after any patch — this confirms the register is the surface (vs. a
panel-side default) and catches the case where line 76483 already forced white.

## (c) Root on the two Linux systems — genuinely easy

Both Linux SoCs are rebuildable from unsigned images, so we don't need to crack
anything — we **add our own access and reflash**.

**AM8250 (Network FW):**
- `rootfs` has `/etc/passwd` with a single `root` account and a classic
  **MD5-crypt** hash in `/etc/shadow`. We don't need to recover it: just replace
  the hash with our own (or blank it) when repacking.
- `/etc/init.d/rcS` even has a **commented-out `/usr/sbin/telnetd`** and
  `inittab` has a commented `getty` on `ttyS0` and an `askfirst` shell. Busybox
  includes `telnetd`. So "get a shell" = uncomment telnetd (or add a dropbear),
  set our own root password, rebuild the ext2 + container (easy — only a 32-bit
  sum), flash via USB. The serial console is also one uncomment away.
- Caveat: confirm `am7x_ota_upgrade.ko` doesn't add a signature check beyond the
  container sum before flashing (see rebuilding §2).

**RTD1295 (Actiontec FW):** `secure_boot=0`, empty `signature`, Android-based,
GOLD rescue image present (recoverable). Same playbook once the ext4 opens with
a newer extractor. This is the **more powerful** SoC (quad A53, RTL8822BE Wi-Fi)
— the better home for "run our own streaming code."

**Recommendation:** prove the pipeline on the AM8250 first (simplest container,
telnetd already present, rescue partitions exist), then move streaming work to
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
   decodes it. Still needed for (b): a string-table/offset locator to handle a
   length-changing edit.
2. ~~Solve the scaler header field `0x1b41bbca`~~ **DONE** — it is CRC-32-MSB
   over `[0x20080..0xa9cf9)` (see rebuilding.md). Scaler repack is unblocked.
3. Write the `MERGE.bin` repacker (in `mst9u_merge.py`) and prove a byte-identical
   round-trip → unblocks scaler data edits (a/b).
4. Write `tools/act_repack.py` and prove a byte-identical round-trip → unblocks
   (c) and (d) on the AM8250.
5. ~~Load the NXP image into Ghidra~~ **DONE** — NXP fully disassembled and
   decompiled (`decompiled/nxp_app.c`, `docs/nxp-mcu.md`). Nuvoton M451 still to
   do (also stock Cortex-M) for (d)/(f).
6. Get `rootfs.ext4.bin` (RTD1295) open with a current extractor → unblocks the
   streaming home for (c).
