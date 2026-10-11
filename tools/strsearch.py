#!/usr/bin/env python3
"""Search binaries for text in ASCII, UTF-16LE and UTF-16BE (case-insensitive).

usage: strsearch.py PATTERN[,PATTERN...] FILE...
"""
import re
import sys
from pathlib import Path

CTX = 40


def show(buf, start, end, enc):
    lo, hi = max(0, start - CTX), min(len(buf), end + CTX)
    if enc != "ascii":
        lo -= (start - lo) % 2
        text = buf[lo:hi + (hi - lo) % 2].decode(enc, "replace")
    else:
        text = buf[lo:hi].decode("latin-1")
    return "".join(c if c.isprintable() else "." for c in text)


def main():
    pats = sys.argv[1].split(",")
    for f in sys.argv[2:]:
        buf = Path(f).read_bytes()
        for pat in pats:
            for enc in ("ascii", "utf-16-le", "utf-16-be"):
                rx = re.compile(re.escape(pat.encode(enc)), re.I)
                hits = list(rx.finditer(buf))
                for h in hits[:6]:
                    print(f"{Path(f).name} {h.start():#09x} {enc:9} {pat!r}: {show(buf, h.start(), h.end(), enc)}")
                if len(hits) > 6:
                    print(f"{Path(f).name} ... {len(hits)} hits for {pat!r} ({enc})")


if __name__ == "__main__":
    main()
