# Feature a (no-signal background colour) — status

**Goal:** replace the bright blue "No … signal" background (and the white
searching screen) with black. Authorised owner modification.

**State (2026-10-12): SOLVED, hardware-confirmed.** Build **v11** shows plain
black on both the searching and no-signal screens, with the Dell logo hidden and
the no-signal text still drawn. Full explanation and the patch table:
[feature-a-osd-model.md](feature-a-osd-model.md).

## The short version

- The no-signal blue was not a colour register at all. In steady no-signal the
  display windows are switched back to **video**, and the no-input video path
  outputs pure blue. Each window can instead show a per-window packed 3-3-2
  "no-signal colour"; `win_set_show_video` (`0x1ac05b`) selects which.
- `0x90102f33` is the **frame colour**: visible only where no display window
  covers the panel. That is the white around the logo on the searching screen,
  and it is why writing it from the no-signal page never showed.
- The no-signal logo is OSD text (a 24×16 glyph block, white on Dell blue from
  the OSD palette), drawn above everything else.

## The fix (v11)

1. **Gate** (`0x1ac05b`): while `g_osd_mode` is 4 or `0x0b`, force "show colour"
   so the windows cannot return to blue video.
2. **Colour** (`0x1c7004`, inside the real setter `0x1c6fe9`): every background
   colour write forced to `0x00`; init-time writes patched to match.
3. **Searching white**: `osd_set_frame_color_rgb` (`0x1c70ec`) always writes
   black.
4. **Logo**: icon colours at `0x2da230` / `0x2da0ab` set to black-on-black.

Patches: `tools/aeon-ghidra/patches/feature-a-v11-allbg-black.lst` (+`.asm.txt`)
and `feature-a-v11-black-data.txt`; feature **b** (`feature-b-unplug.txt`) is
folded in. Release image is built with `NOSTAMP=1` (the version stamp corrupts
the neighbouring text): `dist/scaler/MERGE.bin`, sha256
`918612e2f5bb058adf54a758e9650b201f0f124f86bb8f370cf4c1687b89135b`. The stamped
v11 (`f5eeb886…`) is the one whose behaviour was observed; the unstamped image
differs only by the 12 stamp glyphs.

Known side effects and the one residual risk (picture staying black if the
firmware unmutes before leaving the no-signal page — not observed) are listed in
feature-a-osd-model.md §4.

## Attempt log (all flashed on hardware)

| # | what the build did | result |
|---|---|---|
| v1 | frame colour `0x90102f33` = red, at `osd_nosig_draw` | blue |
| v2 | `osd_set_bg_color_332(0, 0xe0)` at `osd_nosig_draw` | red flash on input switch, then blue; logo/text corrupted |
| v3 / diag | `?` icon at the logo slot, version stamp | proved the cave runs and which build is flashed |
| v4 | `osd_set_frame_color_rgb(0,0,0)` at `osd_nosig_draw` | blue |
| v5 | same, hooked at `osd_no_input_handler` | blue; searching white |
| v6 | `osd_set_bg_color_332(0,0xe0)` at `osd_no_input_handler` | built; no result recorded |
| v7 | `osd_set_frame_color()` + frame colour red | blue; searching white |
| v8 | v7 + OSD window 1 on (then thought to be "un-mute ch1") | blue; searching white |
| v9 | `win_set_show_video(0xf,0)` + frame colour red | blue; searching white |
| **v10** | gate + every 3-3-2 colour forced to `0xe0` | **no-signal red and held**; searching white with a red logo rectangle; no-signal logo white on blue |
| **v11** | v10 in black + frame colour black + logo hidden | **searching and no-signal black, logo gone** |

Why each earlier build failed is in feature-a-osd-model.md §5.

## Lessons for the next reverse-engineering job on this image

- The flow disassembler misses code reached only through data pointers (menu
  item callbacks, icon getters). Check raw bytes before concluding "nothing
  references this".
- The decompiler inlines shared tails: several "functions" in the `.c` are one
  real function (`0x1c6fe9`) reached by tail-jumps. Patch the real one.
- Recognise the vendor framework early. This is the stock MStar monitor OSD
  (menu page table, items, compressed fonts, palette, OSD windows); naming it
  correctly would have saved most of the nine failed builds.
- When one build shows a visible change, rebuild exactly that and vary one
  thing — see the memory note `recreate-before-theorising`.
