# Feature (a) — hunting the STABLE no-signal blue

> **SUPERSEDED (2026-10-12).** Feature a is solved; see
> [feature-a-osd-model.md](feature-a-osd-model.md). This file is an earlier
> analysis kept for history. What it got right: `FUN_ram_001ac05b` is the gate
> between "video" and "colour" for a display window. What it got wrong:
> - the colour shown when the gate is clear is the per-window **3-3-2 byte**,
>   not `0x90102f33`; `0x90102f33` is the frame colour, visible only where no
>   window covers the panel;
> - the steady blue is not "a layer below the windows" — it is the windows
>   showing **video** with no input;
> - `0x101d20` is OSD window 1, not a scaler channel, so the "channel mute"
>   risk was not real;
> - its "next experiment" (v7) was flashed and did not work — the cave ran once
>   and the firmware switched the windows back to video afterwards. The fix was
>   to hook the gate itself.

Dedicated trace to find the surface that holds the **stable blue** behind the
"No … signal" screen, after two hardware disproofs (see
[feature-targets.md](feature-targets.md) §(a) for the history). Written
2026-10-12.

All line numbers are in `firmware/unpacked/scaler/ap_combined.bin.aeon.c`.
RAM base = `0x180000`; file offset = RAM − `0x180000`.

---

## TL;DR — the decisive finding

**The free-run frame-colour register `0x90102f33` is only *displayed* when a
per-window "show frame-colour" bit is asserted. In the no-signal (mode-4) path
that bit is NEVER asserted; in the searching (mode-0x0e) path and the burn-in
(mode-0x0d) path it IS.** That single difference explains every hardware result
so far, and it means **both prior tests (v1, v2) were missing the one call that
actually routes a colour to the panel: `FUN_ram_001ac05b(mask, 0)`** (the
"Enable Frame Color" window-enable at `0x1ac05b`).

- The visible blue is **not** `0x90102f33` (proven: writing it red/black did
  nothing in mode 4).
- The visible blue is the layer shown when the windows display **neither video
  nor frame-colour** — i.e. the display/canvas background *below* the windows.
  Its blue value is **a reset/boot default**; no AP instruction ever writes a
  blue literal (confirmed below), and `0x90102f33` never appears as an init data
  word.
- **You do not need to find the blue register to beat it.** Re-enabling
  frame-colour display with a red value in the mode-4 path paints red *over* the
  blue, exactly as the searching screen paints white over it. That is the single
  most-promising experiment (§"Next experiment").

Confidence: the *mechanism* (frame-colour display gate) is **proven from code**
and corroborated by three independent screens (searching=white, burn-in=cycles,
no-signal=blue). Whether re-enabling it is *sufficient* in mode 4 — given that
mode 4 also mutes the scaler channel — is **inferred, not proven**; the
experiment is designed to settle it in one flash.

---

## The mechanism (proven from code)

The free-run frame colour is a **composite of four things**, all keyed on a
per-window mask (`uRam0030f6da` / the `g_window_ctx` array, stride `0x70`):

1. **Per-window "show frame-colour" enable bit** — reg
   `g_window_ctx[win] - 0x6fffffce`, mask `0x20` (even win) / `0x2` (odd win).
   Written only by `FUN_ram_001ac05b(mask, enable)` @ `0x1ac05b` (line 33517):
   `enable==0` → CLEARS the bit (= **show frame colour**); `enable!=0` → SETS it
   (= show video). (`FUN_ram_001c61c5` sets bits if arg≠0, else clears — see
   symbols.tsv.)
2. **Per-window background byte** — `-0x70ffffd1` (even) / `-0x6fffffb8` (odd),
   written `0xff` for opaque.
3. **The RGB value** — `scaler_reg_write24(0x90102f33, R|G<<8|B<<16)` (f33=R,
   f34=G, f35=B; confirmed by both `osd_set_frame_color_rgb` @0x1c70ec line
   57746 and the 3-3-2 expansion in `osd_set_bg_color_332` line 50613).
4. **Per-window priority/latch** — `FUN_ram_001c63ce(0xf,1)` + per-window RMW at
   `-0x7fffff7c`.

**The searching screen (mode 0x0e) does all four.** `osd_no_input_handler`
(`0x1e73fe`) line 76414 calls `osd_set_frame_color` (`0x1ff83c`), which (lines
90902-90974) calls **`FUN_ram_001ac05b(uRam0030f6da, 0)`** [step 1 — enable],
writes the bg bytes `0xff` [step 2], writes `0x90102f33 = 0xffffff` [step 3,
line 90944], and sets priorities [step 4]. Result: a solid **white** screen.

**The burn-in cycle (mode 0x0d) does steps 2-4 but NOT step 1** — because frame
colour is already enabled when it is entered. `osd_nosignal_color_cycle`
(`0x1e76f7`) line 76663 calls `osd_set_bg_color_332(0, g_nosig_bg_colors[i])`,
which (line 50582) writes bg bytes + `0x90102f33` + latch but **never calls
`FUN_ram_001ac05b`**. This path *visibly cycles* black/blue/red/green through
`g_nosig_bg_colors` (`0x2cf6e4`) — **proving `0x90102f33` CAN drive a full-screen
solid colour (including red and blue) when the window enable bit is already
set.**

**The no-signal screen (mode 4) does NONE of step 1-4 for colour.** In
`osd_no_input_handler` the mode-4 branches (lines 76450-76473) call
`scaler_setup_17b6()` (`0x1e17b6`, line 75214) — which only **disables scaler
channel 1** (`scaler_reg_write(0x101d20,0,1)`, bit0=0) — and in one sub-branch
`osd_set_bg_color_332(0,0xff)` (line 76467). **`FUN_ram_001ac05b` is never
called on any mode-4 path.** The draw handler `osd_nosig_draw` (`0x1dbc84`, line
71302) only streams the two GRAM bitmaps + text; it touches no colour register.

### Why v1 and v2 failed — reconciled

| Test | What it wrote | Missing | Result |
|------|---------------|---------|--------|
| v1 — bare `scaler_reg_write24(0x90102f33, red)` | step 3 only | steps 1,2,4 | blue (colour not displayed) |
| v2 — `osd_set_bg_color_332(0, red)` | steps 2,3,4 | **step 1 (`FUN_ram_001ac05b`)** | transient red on switch, then blue |

v2's **transient red** is the tell: for a moment during the input switch the
windows happened to be showing frame-colour (enable bit still clear from a prior
state), so the red flashed; then the mode-4 setup left the windows not showing
frame-colour, and the blue below re-appeared. Neither test ever asserted the
window enable bit itself, which is the thing the *working* white screen does.

---

## Candidate surfaces, ranked

### #1 — Frame-colour display gate (re-enable + red). CONFIDENCE: HIGH (mechanism proven; sufficiency inferred)
- **Registers:** `g_window_ctx[win] - 0x6fffffce` (enable bit, via
  `FUN_ram_001ac05b`) **+** `0x90102f33` (RGB) **+** per-window bg bytes
  `-0x70ffffd1`/`-0x6fffffb8` **+** priority `-0x7fffff7c`.
- **Red value:** `0x90102f33 = 0x0000ff` (f33=R=0xff, f34=G=0, f35=B=0); bg
  bytes = `0xe0` (3-3-2 red) or keep `0xff`.
- **Evidence:** proven from code (lines above) and triangulated by three live
  screens. This is the *only* RGB colour surface the AP ever drives.
- **Risk / unknown:** mode 4 also mutes channel 1 (`0x101d20` bit0=0) via
  `scaler_setup_17b6`; the searching screen does **not** mute it. If a muted
  channel suppresses frame-colour output, re-enabling the gate alone won't show —
  we'd also need `scaler_reg_write(0x101d20,1,1)` (re-enable ch1). The experiment
  below tests this directly.

### #2 — Display/canvas background below the windows (the actual blue). CONFIDENCE: MEDIUM that it exists; LOW that we can name it statically
- **What:** the layer shown when windows display neither video nor frame colour.
  Its value is **blue by reset default** — there is **no AP write of a blue
  literal anywhere** (every `scaler_reg_write24` call site is `0x90102f33`
  white/computed, the OD regs `0x96102f72/76`=0, or timing regs `0x14050c/10/1e`;
  `0x90102f33` never appears as an init data word in the `.asm`).
- **Evidence (inferred):** by elimination — v1+v2 prove it's below `0x90102f33`
  and below the per-window bg bytes; #1's gate is the thing that covers it.
- **Why not directly patchable:** a reset default isn't an AP instruction, so the
  only AP-side fix is to actively cover it each frame (= candidate #1). A
  **serial register dump at no-signal** is the right way to name it empirically
  (read the display/MOD bank around `0x101e`/`0x102e`, and `0x90102f33`, before
  and after candidate #1).

### #3 — Per-window 24-bit colour `-0x6fffffcd`. CONFIDENCE: LOW
- **Where:** main display init writes `scaler_reg_write24(iVar16 - 0x6fffffcd,
  0xffffff)` for odd windows (line 32313), alongside bg byte `-0x6fffffb8=0xff`.
- **Assessment:** a per-window matte/border colour, set **white** at init — so
  unlikely to be the blue source, but it is a distinct colour reg never touched
  by v1/v2. Worth a cheap try (write it `0x0000ff` red) only if #1 fails.

### #4 — Output-mux `0x90102f43` (mask 0x30: 0x00/0x10/0x20). RULED OUT as the blue
- Only callers are `Power_TurnOffPanel`/`Power_TurnOnPanel` via `FUN_ram_001c549f`
  (lines 65899/65934/65947) — a **black mute during panel power transitions**,
  not a no-signal background. Mention only to close it out.

### #5 — 8051 cores (sboot.bin / pm_8051.bin). RULED OUT as a code source
- No `blue`/`background`/`nosignal`/`mute`/`frame`/`0x102f` strings or colour
  inits in `sboot.bin.8051.c` or `pm_8051.bin`. If the blue is a reset default
  (candidate #2), it is a **hardware** default, not an 8051 write we can patch.

---

## The single most-promising next experiment

**Hook `osd_nosig_draw` (`0x1dbc84`, file off `0x5dc84`) — the existing cave at
`0x182048` already runs here every no-signal draw — and call the unmodified
searching-path function `osd_set_frame_color` (`0x1ff83c`) first.**

Decision tree (one flash settles the whole question):

1. **Screen turns WHITE** → PROVEN: the frame-colour display gate is the
   no-signal surface and the channel mute does *not* block it. Then flip the
   colour to red: either
   - patch the immediate `0xffffff` at the `scaler_reg_write24(0x90102f33, …)`
     inside `osd_set_frame_color` (line 90944) to `0x0000ff`, **or**
   - in the cave, after calling `osd_set_frame_color()`, append
     `FUN_ram_001c63ce(0xf,1); scaler_reg_write24(0x90102f33, 0x0000ff);`.
2. **Screen STAYS BLUE** → the channel mute (`0x101d20` bit0=0) is suppressing
   frame-colour output, or the blue is a deeper surface. Next flash: in the cave
   also re-enable ch1 — `scaler_reg_write(0x101d20,1,1)` (or
   `scaler_ch_set_enable(1,1)` @ `0x1c369a`) — *before* `osd_set_frame_color()`,
   and retest. If it then goes white/red, the fix is gate + channel re-enable.
   If still blue, escalate to a **serial register dump** at no-signal to name
   candidate #2 directly.

**Concrete first build:** cave at `0x182048` (mode-4 path), body =
`osd_set_frame_color()` call, nothing else. White proves the mechanism with zero
colour-encoding ambiguity; red is a one-word follow-up.

### Key addresses (RAM / file-offset)
| Symbol | RAM | file off |
|--------|-----|----------|
| `osd_nosig_draw` (hook) | `0x1dbc84` | `0x5dc84` |
| existing cave | `0x182048` | `0x2048` |
| `osd_set_frame_color` (call this) | `0x1ff83c` | `0x7f83c` |
| `osd_set_frame_color` white write | line 90944 | — |
| `FUN_ram_001ac05b` (frame-colour enable) | `0x1ac05b` | `0x2c05b` |
| `scaler_setup_17b6` (channel mute) | `0x1e17b6` | `0x617b6` |
| `scaler_ch_set_enable` | `0x1c369a` | `0x4369a` |
| `osd_no_input_handler` | `0x1e73fe` | `0x673fe` |
| `osd_set_bg_color_332` (v2) | `0x1be86b` | `0x3e86b` |
| ch1 enable reg | `0x101d20` bit0 | — |
| frame-colour RGB reg | `0x90102f33` | — |

---

## Honest uncertainties (we've been burned by confident guesses)

- **Proven:** the frame-colour display gate (`FUN_ram_001ac05b`) is present in
  searching/burn-in and absent in every mode-4 path; `0x90102f33` can drive a
  full-screen solid colour when that gate is set (burn-in cycle); no AP code
  writes a blue literal.
- **Inferred, NOT proven:** that re-enabling the gate in mode 4 is *sufficient*
  — the concurrent channel-1 mute may force the window off regardless. The
  experiment's step-2 branch is there precisely because this could fail.
- **Unknown:** the identity/value of the actual blue register (candidate #2).
  Only a serial dump can name it; static analysis finds no literal. Do not assert
  it is `0x90102f33`'s default — HW tests already contradict that.
- Byte order of `0x90102f33` (f33=R) is corroborated by two independent code
  paths but white is symmetric; the burn-in table `g_nosig_bg_colors` (blue at
  [1]/[5], red at [3]) is the cross-check if red/blue come out swapped.
