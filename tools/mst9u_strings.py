#!/usr/bin/env python3
"""Dump the strings in the decompressed MST9U scaler AP.

Writes two tab-separated files (offset, text) into OUTDIR:
  menu_strings.tsv   OSD/menu text, stored as font glyph indices
  ascii_strings.tsv  plain ASCII debug/log strings from the whole image

Glyph codes with no known character are written as <xx>. The glyph map was
worked out from context (see docs/firmware-components.md, "Menu text
encoding"); entries marked "?" below are probable, not confirmed.

usage: mst9u_strings.py ap_decompressed.bin OUTDIR [--menu START:END] [--min N]
"""
import argparse
import re
from pathlib import Path

MENU_START, MENU_END = 0x164000, 0x170000

GLYPHS = {0x01: " ", 0x04: "'", 0x05: "(", 0x06: ")", 0x07: "+", 0x08: ",",
          0x09: "-", 0x0A: ".", 0x0B: "/", 0x16: ":",
          0x17: ">",  # ?
          0x18: "?",  # ?
          0x19: "@",  # ?
          0x4E: "ç", 0x4F: "ñ", 0x50: "…", 0x58: "¿", 0x5A: "á", 0x5B: "é",
          0x5C: "í", 0x5D: "ó", 0x5E: "ú",
          0x5F: "«", 0x60: "»",  # ?
          0x61: "É", 0x62: "à", 0x63: "â", 0x64: "è", 0x65: "ê", 0x66: "’",
          0x68: "Ä", 0x6A: "ß", 0x6B: "ä", 0x6C: "ö", 0x6D: "ü"}
GLYPHS.update({0x0C + i: str(i) for i in range(10)})
GLYPHS.update({0x1A + i: chr(65 + i) for i in range(26)})
GLYPHS.update({0x34 + i: chr(97 + i) for i in range(26)})


def decode(raw):
    return "".join(GLYPHS.get(b, f"<{b:02x}>") for b in raw)


def menu_strings(buf, start, end):
    pos = start
    for chunk in buf[start:end].split(b"\0"):
        if chunk:
            yield pos, decode(chunk)
        pos += len(chunk) + 1


def ascii_strings(buf, minimum):
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minimum, buf):
        yield m.start(), m.group().decode("ascii")


def write(path, rows):
    n = 0
    with path.open("w", encoding="utf-8") as f:
        f.write("offset\ttext\n")
        for off, text in rows:
            f.write(f"0x{off:06x}\t{text}\n")
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", type=Path)
    ap.add_argument("outdir", type=Path)
    ap.add_argument("--menu", default=f"{MENU_START:#x}:{MENU_END:#x}",
                    help="menu text range START:END")
    ap.add_argument("--min", type=int, default=5, help="minimum ASCII string length")
    args = ap.parse_args()

    buf = args.image.read_bytes()
    start, end = (int(x, 0) for x in args.menu.split(":"))
    args.outdir.mkdir(parents=True, exist_ok=True)
    n = write(args.outdir / "menu_strings.tsv", menu_strings(buf, start, end))
    print(f"menu_strings.tsv   {n} strings from {start:#x}-{end:#x}")
    n = write(args.outdir / "ascii_strings.tsv", ascii_strings(buf, args.min))
    print(f"ascii_strings.tsv  {n} strings")


if __name__ == "__main__":
    main()
