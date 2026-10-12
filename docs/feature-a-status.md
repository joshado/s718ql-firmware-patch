# Feature a (no-signal background colour) — status & handoff

**Goal:** change the projector's "No … signal" screen background from **blue** to
**red** (chosen) — ultimately black/any colour. Authorised owner modification.

**Current state (2026-10-12): NOT solved on hardware.** Nine builds flashed; none
changed the *stable* no-signal blue. One build (osd_set_bg_color_332) produced a
**brief red flash on input-switch** — the only visible colour change so far, and
the strongest lead. See the attempt log and "the red we saw" below.

> **Process note for the next session:** when the user asks to *re-create a
> specific build we already saw work*, build exactly that first — don't pivot to a
> new theory. This session burned flashes going in circles instead of reproducing
> the known-good red. See [[recreate-before-theorising]].

---

## The screen (what we know for sure)

The no-signal screen is a **two-layer composite**:
- **Foreground (OSD):** the white Dell logo + "No HDMI n (x.y) signal / Press
  Source…" text, drawn by `osd_nosig_draw` (`0x1dbc84`, mode 4) into OSD GRAM.
  Transparent background (index 0), so the layer below shows through.
- **Background (the blue):** a layer **below the OSD**. This is the thing we need
  to recolour, and it is **NOT** any surface we've managed to write.

Related screens:
- **Searching screen** (briefly, on source change) = solid **white**, painted by
  `osd_set_frame_color` (`0x1ff83c`) → frame-colour register `0x90102f33` **with**
  the per-window display-enable `FUN_ram_001ac05b`. This screen DOES respond to
  `0x90102f33` (it's white because that reg is white + enabled).
- **Burn-in / colour-cycle** (mode 0x0d) visibly cycles black/blue/red/green by
  writing `0x90102f33` from `g_nosig_bg_colors` (`0x2cf6e4`) — proving `0x90102f33`
  *can* paint a full-screen solid colour (incl. red) **when the enable is on**.

## Key registers / functions

| thing | addr | role |
|---|---|---|
| `osd_nosig_draw` | `0x1dbc84` | mode-4 no-signal draw handler (our usual hook) |
| `osd_no_input_handler` | `0x1e73fe` | no-input state machine (sets mode, calls setup) |
| `scaler_setup_17b6` | `0x1e17b6` | mode-4 setup; **mutes ch1** (`0x101d20` bit0=0) |
| `scaler_ch_set_enable` | `0x1c369a` | ch enable/mute: bit0 of `0x101d00+ch*0x20` |
| `osd_set_frame_color` | `0x1ff83c` | searching-white: enable + `0x90102f33`; has an **early-return** `if DAT+0x45c != 0` |
| `FUN_ram_001ac05b` | `0x1ac05b` | **frame-colour display ENABLE** (per-window bit, mask arg) |
| `osd_set_bg_color_332` | `0x1be86b` | per-window **OSD-window** bg (3-3-2 colour); the only thing that flashed red |
| `osd_set_frame_color_rgb` | `0x1c70ec` | writes `0x90102f33` (RGB) **without** the enable |
| `scaler_reg_write24` | `0x1c64ed` | write 24-bit value to reg,reg+1,reg+2 |
| frame-colour reg | `0x90102f33` | RGB888 `V = R|G<<8|B<<16`; red = `0xff`, blue(default) `0xff0000` |
| window mask | `0x30f6da` | `uRam0030f6da`, u16, the window mask passed to the enable |

## Attempt log (all flashed on hardware)

| # | what the cave did (hook) | result |
|---|---|---|
| v1 | `scaler_reg_write24(0x90102f33, red)` — no latch/enable (osd_nosig_draw) | blue (no change) |
| v2 | `osd_set_bg_color_332(0, 0xe0=red)` (osd_nosig_draw) | **transient RED on switch → blue**; logo/text corrupted. **"the red we saw."** |
| v4 | `osd_set_frame_color_rgb(0,0,0=black)` (osd_nosig_draw) | blue |
| v5 | `osd_set_frame_color_rgb(0,0,0)` (osd_no_input_handler, state) | blue; searching white |
| v7 | `osd_set_frame_color()` + override `0x90102f33`=red (osd_nosig_draw) | blue; searching white |
| v8 | v7 + `scaler_ch_set_enable(1,1)` (un-mute ch1) first | blue; searching white |
| v9 | `FUN_ram_001ac05b(0xf,0)` enable + `0x90102f33`=red (osd_nosig_draw) | blue; searching white |

(v3 and the "diag" builds were icon/execution-proof + version-stamp experiments,
not colour.)

## What each result tells us

- `0x90102f33` alone (v1) = never displayed on no-signal (needs the enable; and
  even enabled it may be the *searching* surface, not the no-signal one).
- `osd_set_bg_color_332` (v2) = drives the **OSD-window** background, which is a
  layer **above** the no-signal blue and goes **transparent** in steady state →
  red only shows during the switch transition, then the blue below returns.
- `osd_set_frame_color` / `_rgb` / the direct enable (v4/v5/v7/v8/v9) = all target
  `0x90102f33` / its enable; none changed the no-signal blue. Likely either
  `osd_set_frame_color`'s early-return fires in mode 4, or — more likely — the
  **stable no-signal blue is a different, lower surface** (video-mute / panel
  default) that `0x90102f33` does not drive. v9 called the enable directly and
  still got blue, which argues the no-signal blue is simply not the frame-colour
  layer.

## Open challenges (the real blockers)

1. **Where is the stable no-signal blue actually generated?** Not `0x90102f33`
   (v1/v4/v5/v7/v8/v9), not the OSD window bg except transiently (v2). Prime
   suspects: a scaler **video-mute / blank-colour** register, or a **panel/TCON**
   free-run default. No AP code writes a blue literal anywhere (confirmed), so it
   is likely a **reset/boot default** of some lower-layer register.
2. **Why is osd_set_bg_color_332's red only transient?** The OSD window bg is
   opaque briefly during the switch, then the window goes transparent (index 0)
   so the blue below shows. Would forcing the window **opaque/continuously
   re-applied** hold the red? (Untried cleanly — see next steps.)
3. **Static-analysis reliability.** The decompiler (`.c`) *inlines* the colour
   helpers and the flow-disassembler (`.asm`) leaves **gaps** exactly in
   `osd_set_frame_color`'s body (indirect control flow). Linear `AeonDisasmAt`
   **misaligns** there. Don't trust `.c` bodies for these functions for
   instruction-level patching — cross-check against the `.asm` and raw bytes.

## Recommended next steps (in order)

1. **SERIAL REGISTER DUMP (definitive).** Hook the scaler debug UART (`dbg_print`
   path; console gated by a GPIO at `scaler_mmio+0x8a9`). At the **white searching**
   screen vs the **blue no-signal** screen, dump `0x90102f30`–`0x90102f50` and the
   per-window enable bits. The register that is blue on no-signal but not on
   searching IS the target. This ends the guessing in minutes vs more blind flashes.
2. **Re-create & study the transient red** (the strongest visual lead): flash the
   `osd_set_bg_color_332` red build (on the USB now, sha `bc1e5126…`), watch the
   transition frame-by-frame, and try making that window **opaque and
   continuously re-applied** (e.g. hook a periodic OSD redraw, not the one-shot
   draw) to see if red can be *held*.
3. If serial isn't available, trace the **video-mute / blank path** from
   `scaler_setup_17b6` (mode-4 channel mute) and the `0x90102f4x` block siblings of
   `0x90102f33` (e.g. `0x90102f43` source-select written `0x20/0x10/0x00`) for a
   mute/blank-colour register.

See also: `docs/feature-a-blue-hunt.md` (the sub-agent's ranked analysis — note it
relied on the `.c`, whose `osd_set_frame_color` body is partly inlined), and
`docs/feature-targets.md` section (a).

## Artifacts

- Patches: `tools/aeon-ghidra/patches/feature-a-nosig-*.lst` (+ `.asm.txt`). The
  **red** one is `feature-a-nosig-red.lst` (osd_set_bg_color_332 @0x1be86b).
- Build: `tools/build_scaler.py` (see rebuilding.md §1 "Build & flash a scaler
  patch"). Current `dist/scaler/MERGE.bin` = the red build, sha `bc1e5126…`.
- feature **b** ("NEVER unplug") is folded into every build and is
  hardware-confirmed working.
