# Feature (a) — how the no-signal screens are composed, and the fix

**Status (2026-10-12): SOLVED and hardware-confirmed.** Build v11 makes the
searching and no-signal screens plain black with the logo hidden. This is the
authoritative write-up; [feature-a-status.md](feature-a-status.md) has the
short version and the full attempt log, and
[feature-a-blue-hunt.md](feature-a-blue-hunt.md) is a superseded earlier
analysis kept for history.

Line numbers are in `firmware/unpacked/scaler/ap_combined.bin.aeon.c`; addresses
are RAM (`file offset = RAM − 0x180000` in `ap_combined.bin`).
Tags: **[code]** read from the decompile/asm/raw bytes; **[hw]** observed on the
projector; **[inferred]** reasoning not directly tested.

## 1. The display layers

Per scaler, bottom to top. `ctx` = `g_window_ctx[n]` (a `0x102f00`-style bank
base); the top byte of an encoded register address selects the sub-bank.

1. **Frame colour** — `0x90102f33/34/35` (bank 0x10, R/G/B, 24-bit). Shown
   wherever **no display window covers the panel**. **[code]+[hw]**
2. **Display window** (main/sub per scaler). Window enable bits: bank 0x20 byte
   `0x20` bits 0/1 (`ctx − 0x5fffffe0`). Each window shows one of:
   - **video**, when its bit in bank 0x10 byte `0x32` is **set** (`0x20` for an
     even window index, `0x02` for odd; `ctx − 0x6fffffce`), or
   - its **no-signal colour**, a packed 3-3-2 byte (bits 7-5 R, 4-2 G, 1-0 B),
     when that bit is **clear**: bank 0x0f byte `0x2f` (even,
     `ctx − 0x70ffffd1`) / bank 0x10 byte `0x48` (odd, `ctx − 0x6fffffb8`).
   **[code]+[hw]**
3. **OSD** — MStar character OSD: windows 1..3, text cells, mono/colour fonts,
   a 248-entry palette. **[code]**

Key functions:

| addr | name in `symbols.tsv` | what it really does |
|---|---|---|
| `0x1ac05b` | `win_set_show_video` | for each window in `mask`: `v=1` set the video bit, `v=0` clear it (show the 3-3-2 colour). The mute/unmute gate. |
| `0x1c6fe9` | `win_set_nosig_color_332` | the one real colour setter `(win_group, colour332)`: writes the per-window 3-3-2 bytes **and** the frame colour (expanded from the same 3-3-2 value). |
| `0x1be86b` | `osd_set_bg_color_332` | thin wrapper: wait (`FUN_ram_001d11b0`) then tail-jump to `0x1c6fe9`. |
| `0x1c7137` | `win_enable_frame_color` | "Enable Frame Color" `(win, v)`: `win_set_show_video(mask, v)`; if `v==0` also `win_set_nosig_color_332(win, 0xff)`. |
| `0x1ff83c` | `osd_set_frame_color` | `win_enable_frame_color(i, 0)` for window 0, or all four when `g_sys_ctx+0x45c != 0`. |
| `0x1c70ec` | `osd_set_frame_color_rgb` | writes only the frame colour `0x90102f33 = R\|G<<8\|B<<16`. |
| `0x1be71a` | `frame_color_fullscreen` | `(1,R,G,B)`: save + clear the window-enable bits, write frame colour → full-screen solid colour; `(0,…)` restores. Used by the 256-step grey ramp at `0x1f00d4`. Not used by the fix. |
| `0x1c63ce` | `scaler_rmw_bit_windows` | toggles the double-buffer bits (bank 0 byte `0x84`) around a write. Not a display gate. |

Frame-colour writers, complete list: `0x1c70c5` (in `0x1c6fe9`), `0x1c711c`
(in `0x1c70ec`), `0x1aaa08` (display init), `0x1be763`/`0x1be7e0` (test ramp).

The committed `decompiled/scaler_app.c` and the local `.aeon.c` predate these
names and show the same code under the old ones (`FUN_ram_001ac05b`,
`FUN_ram_001c7137`, …) with the colour setter inlined into each caller.

## 2. What each screen is

- **Searching screen** (brief, on source change): white with the Dell logo.
  The display window is only the **logo rectangle** (the blue-on-white JPEG,
  TEL id `0xc500`, drawn into frame memory by `FUN_ram_001efda8`); the white
  around it is the **frame colour**, set by
  `osd_set_frame_color_rgb(0xff,0xff,0xff)` from `FUN_ram_002027b0`
  (`0x2027be`). **[hw]** — with every 3-3-2 write forced red (v10) the surround
  stayed white and the logo rectangle went red.
- **No-signal screen** (steady): the display windows cover the panel and are
  switched back to **video**; with no input the video path outputs saturated
  pure blue. **[hw]** — forcing the windows to stay in "colour" mode (v10) held
  red. *Why* the no-input video is blue was never identified (no blue literal
  exists in the AP); it did not need to be.
- **No-signal logo and text**: OSD, above the display window. See §3.

## 3. The OSD (corrects the earlier notes)

This is the standard MStar monitor OSD framework. **[code]**

- **Menu page table** `0x2d9200`, stride `0x1c`, indexed by `g_osd_mode`
  (`0x307b98`): `+0` XSize, `+1` YSize (cells), `+4` previous page, `+8` item
  array, `+0xc` item count, `+0x10` Exec fn, `+0x14` LoadFont fn, `+0x18` flags.
  Mode 4 = no-signal (24×16, 6 items @`0x2d3834`), `0x0b` = the alternate
  no-signal page, `0x0e` = searching, `0x0d` = burn-in cycle, 7 = splash.
- **Items** are `0x2c` bytes: `+0/+1` X/Y, `+4/+5` colour attributes, `+8` a
  callback, `+0x24` pointer to a display-entry table whose entries hold a
  getter (`+8`) returning the thing to draw.
- **Icons** (what those getters return): `{width, height, colour×3, glyph
  codes…}`. Getters are 10-byte `return &icon` stubs (e.g. `0x1e7a96`) reached
  only via pointers, so the flow disassembler never listed them — read raw
  bytes.
- **Colour attribute** for mono fonts = two 4-bit palette indices.
- **Palette**: 248 entries × `{R,G,B,colour-key}` at `0x2d2dc8`, loaded by
  `FUN_ram_001db9a7` (line 71174). Index 0/0xf black, 2 white, 0xc Dell blue
  `00 85 c3`; `c0 c0 c0` + key = transparent.
- **OSD RAM port** `0x101c66/67/68`: `0x101c68` selects 0 = address, 1 = code,
  2 = attribute, 3 = font, `0xa/0xc/0xd` = palette address/key/colour.
  `0x21c925` (`osd_gram_write_rle`) is the compressed **mono font** loader;
  `0x21c727` loads 2-bit colour fonts.
- **`osd_nosig_draw` (`0x1dbc84`)** is mode 4's **LoadFont** callback: it loads
  the logo glyphs at font index `0x20` (and the `?` icon at `0xcf`). It draws
  nothing.
- **OSD window registers** `0x101d00 + n*0x20` (right-half twin at `0x101b..`):
  `0x1c369a` window on/off, `0x1c36b4` ctrl, `0x1c3656` fixed alpha, `0x1c3708`
  text-RAM base, `0x1c378f` fixed colour. `scaler_setup_17b6` is **Osd_Hide**,
  `FUN_ram_001e17e0` is **Osd_Show**. These are not scaler channels.
- **The no-signal logo**: page item 0 shows the icon at `0x2da22e` — a 24×16
  block of glyphs `0x21..0x46`, colour `0xc2` (white on Dell blue). A variant
  at `0x2da0a9` (item 1) uses colour `0x24`. **[code]+[hw]**

## 4. The fix (v11)

Listing `tools/aeon-ghidra/patches/feature-a-v11-allbg-black.lst` (+`.asm.txt`),
data patches `feature-a-v11-black-data.txt`. Build command in
[rebuilding.md](rebuilding.md) §1.

| what | where | change |
|---|---|---|
| Hold the colour on no-signal | hook `0x1ac05b` → cave `0x182048` | `if (g_osd_mode == 4 \|\| g_osd_mode == 0x0b) v = 0;` — windows cannot go back to video (blue) while a no-signal page is up |
| All background colours → black | hook `0x1c7004` → cave `0x182063` | colour argument of `win_set_nosig_color_332` forced to `0x00` (covers the 3-3-2 bytes and the frame colour it derives) |
| Init colours | `0x1aa9f0`, `0x1aad20` | 3-3-2 byte `0xff` → `0x00` |
| Init frame colour | `0x1aaa01`, `0x1aaa04` | `0xffffff` → `0` |
| Searching-screen white | `0x1c7119` in `osd_set_frame_color_rgb` | `b.or r4,r12,r11` → `b.ori r4,r0,0x0` (always black) |
| Hide the no-signal logo | `0x2da230`, `0x2da0ab` | icon colours `c2 c2 c2` / `24 24 24` → `00 00 00` (black on black) |

To use another colour: put the 3-3-2 value in cave C and the init bytes, and
the matching RGB in the `0x1c7119` / `0x1aaa01` immediates (`0xe0` = red was
v10).

**Side effects (by design of the patch):**
- The burn-in colour cycle (mode `0x0d`) uses the same setter, so it is all
  black.
- The boot splash uses the same frame-colour setter, so its white surround is
  black.
- Any brief picture mute during a mode change is black.
- The searching-screen JPEG draw itself is not disabled; the rectangle is black
  because the window stays in "colour" mode there. **[hw]**

**Residual risk [inferred]:** the gate swallows any "show video" call made while
`g_osd_mode` is still 4/`0x0b`. If the firmware ever unmutes before it leaves
the no-signal page, the picture would stay black until the next mode change.
Not observed on hardware in v10/v11. A tighter gate would test the sync-loss
flags (`DAT_ram_002cfc84[port*0x4c] & 0xc`) instead of the OSD mode.

## 5. How the builds read under this model

| build | change | result [hw] | why |
|---|---|---|---|
| v1, v4, v5 | frame colour only, from a mode-4 hook | no change | on the no-signal screen the windows cover the panel, so frame colour is hidden |
| v2 | `osd_set_bg_color_332(0,0xe0)` at `osd_nosig_draw` | red flash on switch, then blue; logo/text corrupted | red showed while the windows were in colour mode; then they went back to video. Corruption is presumed to come from calling the waiting wrapper in the middle of a font load **[inferred]** |
| v7, v8 | `osd_set_frame_color()` then frame colour red | searching white, steady blue | steady state = video again; searching white is re-written by `osd_set_frame_color_rgb` |
| v9 | `win_set_show_video(0xf,0)` + frame colour red | blue | windows were switched back to video after the hook ran |
| v3, diag | icon / version-stamp experiments | — | proved cave execution and flash delivery |
| **v10** | gate + setter forced to `0xe0` | **no-signal red, held**; searching white with red logo rectangle; logo white-on-blue | confirmed §1; exposed the frame-colour surround and the OSD logo |
| **v11** | §4 | **searching and no-signal black, logo gone** | — |
