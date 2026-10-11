#!/usr/bin/env python3
"""Repack an Actions Micro "ActionsFirmware" container (ACTUPGRADE.bin).

Companion to act_unpack.py. The container is just concatenated records plus a
single integrity field: a 32-bit ADDITIVE SUM of the little-endian u32 words
over the body [hdr_len : hdr_len+body_len], stored in the CHECKSUM record at
file offset 0x3c (name "CHECKSUM" at 0x20, then hdr_len/body_len/0/checksum as
<4I at 0x30). For the shipped image hdr_len=0x40, body_len=filesize-0x40, so the
body is [0x40 : EOF] and the checksum field itself (in the 0x40-byte header) is
NOT covered by the sum. Reproduced value for the stock M0R005 image: 0x30cf215e.

Design: this is a "patch the original" repacker, not a "reassemble from parts"
one. It copies the pristine container, splices in any requested partition
replacements AT THEIR RECORD OFFSET (so all inter-record padding, the header and
every untouched record are preserved verbatim), then recomputes and rewrites the
additive sum. With no replacements the output is byte-identical to the input
(the golden-rule round-trip), which also proves the sum algorithm.

Usage:
  act_repack.py <src.bin> <out.bin> [--replace NAME=PATH ...]
  act_repack.py --verify <container.bin>

A replacement file must be EXACTLY the size of the record it replaces (the
partition is a fixed slot in the container); mismatches are rejected.
"""
import argparse
import struct
import sys
from pathlib import Path

REC = 32
CHUNK = 1 << 22
MASK = (1 << 32) - 1


def read_header(f):
    f.seek(0)
    hdr = f.read(0x400)
    magic = hdr[0:16].rstrip(b"\0").decode("ascii", "replace")
    ver = hdr[16:32].rstrip(b"\0").decode("ascii", "replace")
    cname = hdr[0x20:0x30].rstrip(b"\0").decode("ascii", "replace")
    hlen, blen, zero, csum = struct.unpack("<4I", hdr[0x30:0x40])
    return hdr, magic, ver, cname, hlen, blen, zero, csum


def records(hdr):
    for off in range(0x40, 0x400, REC):
        rec = hdr[off:off + REC]
        name = rec[:16]
        if not name.strip(b"\0"):
            break
        label = name.rstrip(b"\0").decode("ascii", "replace")
        a, b, c, d = struct.unpack("<4I", rec[16:])
        yield off, label, a, b, c, d


def additive_sum(f, start, length):
    """Sum of little-endian u32 words over [start:start+length] mod 2**32."""
    total = 0
    carry = b""
    remaining = length
    f.seek(start)
    while remaining > 0:
        chunk = f.read(min(CHUNK, remaining))
        if not chunk:
            break
        remaining -= len(chunk)
        data = carry + chunk
        n = len(data) // 4
        total += sum(struct.unpack("<%dI" % n, data[:n * 4]))
        carry = data[n * 4:]
    if carry:
        # body_len is word-aligned for this format; guard just in case.
        total += int.from_bytes(carry.ljust(4, b"\0"), "little")
    return total & MASK


def verify(path):
    with open(path, "rb") as f:
        hdr, magic, ver, cname, hlen, blen, zero, csum = read_header(f)
        size = Path(path).stat().st_size
        calc = additive_sum(f, hlen, blen)
    print(f"{magic} / {ver}")
    print(f"CHECKSUM rec: hdr_len={hlen:#x} body_len={blen:#x} "
          f"(hdr+body={hlen + blen:#x}, file={size:#x})")
    print(f"stored sum   = {csum:#010x}")
    print(f"computed sum = {calc:#010x}")
    ok = (calc == csum) and (hlen + blen == size)
    print("VERIFY:", "OK" if ok else "FAIL")
    return 0 if ok else 1


def repack(src, out, replacements):
    src = Path(src)
    out = Path(out)
    with src.open("rb") as f:
        hdr, magic, ver, cname, hlen, blen, zero, csum = read_header(f)
    recs = {label: (off, a, b) for off, label, a, b, c, d in records(hdr)}

    # Resolve replacements against the record table first (fail fast).
    plan = []
    for name, p in replacements.items():
        if name not in recs:
            sys.exit(f"error: no record named {name!r} in container")
        off, roff, rsize = recs[name]
        p = Path(p)
        psize = p.stat().st_size
        if psize != rsize:
            sys.exit(f"error: {name}: replacement {p} is {psize:#x} bytes, "
                     f"record slot is {rsize:#x} bytes (must match)")
        plan.append((name, roff, rsize, p))

    # Copy src -> out verbatim (streaming).
    with src.open("rb") as fi, out.open("wb") as fo:
        while True:
            b = fi.read(CHUNK)
            if not b:
                break
            fo.write(b)

    # Splice replacements at their record offsets.
    with out.open("r+b") as fo:
        for name, roff, rsize, p in plan:
            with p.open("rb") as pf:
                fo.seek(roff)
                written = 0
                while written < rsize:
                    b = pf.read(min(CHUNK, rsize - written))
                    if not b:
                        break
                    fo.write(b)
                    written += len(b)
            print(f"spliced {name}: {written:#x} bytes at {roff:#x} <- {p}")

    # Recompute and rewrite the additive sum.
    size = out.stat().st_size
    if hlen + blen != size:
        sys.exit(f"error: hdr_len+body_len ({hlen + blen:#x}) != file size "
                 f"({size:#x}); container layout changed, refusing to sign")
    with out.open("r+b") as fo:
        new_sum = additive_sum(fo, hlen, blen)
        fo.seek(0x3c)
        fo.write(struct.pack("<I", new_sum))
    print(f"old sum = {csum:#010x}  new sum = {new_sum:#010x}"
          f"{'  (unchanged)' if new_sum == csum else ''}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--verify", metavar="CONTAINER",
                    help="verify the additive sum of an existing container")
    ap.add_argument("src", nargs="?", help="pristine source container")
    ap.add_argument("out", nargs="?", help="output container")
    ap.add_argument("--replace", action="append", default=[], metavar="NAME=PATH",
                    help="replace record NAME's payload with file PATH "
                         "(must match the record size); repeatable")
    args = ap.parse_args()

    if args.verify:
        sys.exit(verify(args.verify))
    if not args.src or not args.out:
        ap.error("src and out are required (or use --verify)")

    replacements = {}
    for spec in args.replace:
        if "=" not in spec:
            ap.error(f"--replace expects NAME=PATH, got {spec!r}")
        name, path = spec.split("=", 1)
        replacements[name] = path
    repack(args.src, args.out, replacements)


if __name__ == "__main__":
    main()
