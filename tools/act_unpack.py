#!/usr/bin/env python3
"""Unpack an Actions Micro "ActionsFirmware" container (ACTUPGRADE.bin).

Layout: 32-byte records starting at offset 0.
  0x00  "ActionsFirmware\\0"
  0x10  "UpdateVer:vX.YY\\0"
  0x20  "CHECKSUM" record: hdr_len(u32) body_len(u32) 0 checksum(u32)
  0x40+ component records: name[16] offset(u32) size(u32) arg0(u32) arg1(u32)
  ...   "LINUX" marker followed by partition records:
        name[16] offset(u32) size(u32) part_size(u32) type(u32)
"""
import struct
import sys
from pathlib import Path

REC = 32


def records(buf):
    for off in range(0x40, 0x400, REC):
        rec = buf[off:off + REC]
        name = rec[:16]
        if not name.strip(b"\0"):
            break
        yield off, name, struct.unpack("<4I", rec[16:])


def label(name):
    # Component names are "TYPEname\0..." e.g. "ADFUadfus", "F648brec"
    return name.rstrip(b"\0").decode("ascii", "replace")


def main():
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    size = src.stat().st_size
    with src.open("rb") as f:
        hdr = f.read(0x400)
        print(hdr[0:16].rstrip(b"\0").decode(), "/", hdr[16:32].rstrip(b"\0").decode())
        hlen, blen, _, csum = struct.unpack("<4I", hdr[0x30:0x40])
        print(f"checksum record: hdr_len={hlen:#x} body_len={blen:#x} "
              f"(hdr+body={hlen + blen:#x}, file={size:#x}) checksum={csum:#010x}")
        print(f"{'rec':>5} {'name':<12} {'offset':>10} {'size':>10} {'arg0':>10} {'arg1':>10}")
        in_linux = False
        for off, name, (a, b, c, d) in records(hdr):
            n = label(name)
            print(f"{off:#5x} {n:<12} {a:#10x} {b:#10x} {c:#10x} {d:#10x}")
            if n == "LINUX":
                in_linux = True
                continue
            if n == "reserve" or b == 0 or a + b > size:
                continue
            if out:
                out.mkdir(parents=True, exist_ok=True)
                f.seek(a)
                remaining = b
                dst = out / (("part_" if in_linux else "") + n + ".bin")
                with dst.open("wb") as o:
                    while remaining:
                        chunk = f.read(min(remaining, 1 << 20))
                        if not chunk:
                            break
                        o.write(chunk)
                        remaining -= len(chunk)


if __name__ == "__main__":
    main()
