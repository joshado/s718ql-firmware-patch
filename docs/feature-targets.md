# Feature targets — where each wish lives, and how hard it is

Mapping the wishlist to the responsible processor, with findings so far and a
recommended approach. Difficulty is a rough estimate; "data edit" = no CPU
disassembly needed, "code edit" = needs the target's ISA.

| # | Goal | Lives in | Type | Difficulty |
|---|---|---|---|---|
| a | Change no-input-source background colour | Scaler (MST9U) | data/config edit | medium |
| b | Fix `unplg`→`unplug` shutdown typo | Scaler string table | data edit | low–medium |
| c | Root on the two Linux images | AM8250 + RTD1295 | fs edit + repack | **low** |
| d | Remote power-on for Home Assistant | NXP MCU + AM8250 | code/daemon | high |
| e | HDMI-CEC working | Scaler (CEC) + others | code edit | high |
| e+| HDMI ARC audio to AVR | Scaler/formatter audio | code + HW | very high |
| f | Unresponsive power button | NXP MCU | code edit | medium–high |

---

## (b) The `unplg` typo — most tractable; start here

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

**Partially located.** The scaler is what paints the screen when there's no
signal. Relevant AP strings: `[msScalerAP] Enable Frame Color`, a
`Def*_RedColor/GreenColor/BlueColor` set (colour-temperature defaults), and the
whole **logo-capture** subsystem (`USER_PREF_SYS_DRAW_BMP_LOGO`, `Mute BMP
Logo`, `initScanLogoCapture`). The painfully-bright-blue "no source" field is
most likely either (1) a fixed "**frame colour**" / free-run background the
scaler outputs when it loses sync, or (2) a blue muted-screen fill.

**Open:** find the constant that sets that specific blue. Two sub-paths to
check:
- A hard-coded RGB/colour value fed to "Enable Frame Color" / the free-run
  background register — a data edit once found.
- A user-preference default (the `USER_PREF_SYS_*` block) — possibly even
  changeable without reflashing if it's exposed via a service menu / RS232
  command.

*Next step:* trace "Enable Frame Color" and the mute-screen fill to their colour
argument. This may need light disassembly of the MST9U AP (the hard ISA), unless
the value sits in an obvious config table near the `Def*Color` strings.

---

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

1. Write `tools/mst9u_strcipher.py` (encode/decode the additive-shift OSD text)
   and a string-table locator → unblocks (a) and (b).
2. Solve the scaler header field `0x1b41bbca` → unblocks any scaler repack.
3. Write `tools/act_repack.py` and prove a byte-identical round-trip → unblocks
   (c) and (d) on the AM8250.
4. Load the NXP and Nuvoton images into Ghidra (both are stock Cortex-M) → start
   on (d) and (f) at the source level.
5. Get `rootfs.ext4.bin` (RTD1295) open with a current extractor → unblocks the
   streaming home for (c).
