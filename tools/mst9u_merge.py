#!/usr/bin/env python3
"""Inspect / unpack / REPACK the MStar MST9U scaler image (Projector FW/MERGE.bin).

Known layout (M0R005):
  0x000000  8051 PM (standby) code
  0x010000  header, u32 LE: ap_flash_start, ap_ram_start, ap_ram_end, ap_flash_end,
            0, <header CRC-32 0x1b41bbca>, 0, 0, file_size-4, 0x10000
  0x010000+ sboot (second 8051 program; header above occupies its first 0x80 bytes)
  0x020080  AP blob: 0x3800 bytes uncompressed (exception vectors, image info header at
            +0x1100, resource table "TEL" at +0x2000), then a raw deflate stream
            (0x23880), then u32 LE decompressed size, then CRC-16/CCITT-FALSE (LE)
            over [ap_flash_start, end of deflate stream)
  0x0a9d00+ resources referenced by the TEL table (JPEG logo at 0xb9d08, ...)
  EOF-12    55 AA AB CD magic, EOF-4: CRC-32 (poly 04C11DB7, MSB first, init 0,
            no final xor) over everything before it, stored big-endian

Repacking (see docs/rebuilding.md §1). Two modes:
  * passthrough  (new_ap is None or unchanged): REUSE the original deflate bytes
    verbatim, so the output is BYTE-IDENTICAL to the input. This is the round-trip
    proof that we can reproduce the brick-critical regions (pm_8051/sboot/prefix/
    tail) and every checksum exactly before trusting a flash.
  * patched      (new_ap given): re-DEFLATE the modified AP as a raw stream and
    recompute all four derived fields: AP CRC-16, header ap_flash_end/ap_ram_end,
    header CRC-32 (0x1b41bbca slot), header file_size-4, and the trailer CRC-32.
    The deflate region legitimately differs (our encoder != vendor's); everything
    else outside the AP stays byte-identical.

Note on byte-reproducibility: standard zlib does NOT reproduce the vendor's exact
deflate output, so a *patched* image can only be validated by re-checksum + a
region diff (not by comparing to the original). The passthrough round-trip is what
proves the framing.

CLI:
  mst9u_merge.py <MERGE.bin> [outdir]        inspect (+ unpack components to outdir)
  mst9u_merge.py roundtrip <MERGE.bin>       prove passthrough repack == input
  mst9u_merge.py repack <MERGE.bin> <ap_decompressed.bin> <out.bin>
                                             rebuild with a modified AP; print diff
"""
import binascii
import struct
import sys
import zlib
from pathlib import Path

HDR = 0x10000
AP_PREFIX = 0x3800  # uncompressed head of the AP blob
# header u32 slots (indices into the 10×u32 block at HDR)
H_FSTART, H_RSTART, H_REND, H_FEND, H_HDRCRC, H_FILESZ = 0, 1, 2, 3, 5, 8


def crc32_msb(data, crc=0):
    table = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if c & 0x80000000 else (c << 1) & 0xFFFFFFFF
        table.append(c)
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ table[(crc >> 24) ^ b]
    return crc


def parse(m):
    fstart, rstart, rend, fend = struct.unpack_from("<4I", m, HDR)
    blob = m[fstart:fend]
    pad = AP_PREFIX
    d = zlib.decompressobj(-15)
    ap = d.decompress(blob[pad:])
    stream_end = fend - len(d.unused_data)
    size, crc16 = struct.unpack_from("<IH", m, stream_end)
    return dict(fstart=fstart, rstart=rstart, rend=rend, fend=fend,
                stream_start=fstart + pad, stream_end=stream_end,
                ap=ap, size=size, crc16=crc16)


def raw_deflate(data, level=9):
    """Raw DEFLATE (no zlib header/trailer), matching the on-device inflate (-15)."""
    c = zlib.compressobj(level, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


def repack(orig, new_ap=None):
    """Rebuild MERGE.bin. new_ap=None (or == current AP) -> byte-identical passthrough."""
    p = parse(orig)
    fstart = p["fstart"]
    prefix = orig[fstart:p["stream_start"]]          # 0x3800, never changes

    if new_ap is None or new_ap == p["ap"]:
        deflate = orig[p["stream_start"]:p["stream_end"]]  # reuse verbatim -> identical
        decomp_size = p["size"]
    else:
        deflate = raw_deflate(new_ap)
        decomp_size = len(new_ap)

    body = prefix + deflate                           # the CRC-16 covers [fstart:stream_end]
    crc16 = binascii.crc_hqx(body, 0xFFFF)
    ap_region = body + struct.pack("<IH", decomp_size, crc16)
    new_fend = fstart + len(ap_region)
    new_rend = p["rstart"] + (new_fend - fstart)      # ap_ram_end tracks the flash-region size

    head = bytearray(orig[:fstart])                   # pm_8051 + header + sboot (header is in here)
    struct.pack_into("<I", head, HDR + H_REND * 4, new_rend)
    struct.pack_into("<I", head, HDR + H_FEND * 4, new_fend)
    struct.pack_into("<I", head, HDR + H_HDRCRC * 4, crc32_msb(ap_region))  # 0x1b41bbca slot

    tail = orig[p["fend"]:]                            # resources + trailer (last 4 = file CRC-32)
    merged = bytearray(head + ap_region + tail)
    struct.pack_into("<I", merged, HDR + H_FILESZ * 4, len(merged) - 4)     # file_size-4
    struct.pack_into(">I", merged, len(merged) - 4, crc32_msb(merged[:-4])) # trailer CRC-32 (BE)
    return bytes(merged)


def region_diff(orig, built):
    """Classify where two images differ, by container region. Returns list of (region, note)."""
    po = parse(orig)
    regions = [
        ("pm_8051",        0,                 HDR),
        ("header",         HDR,               HDR + 40),
        ("sboot",          HDR + 40,          po["fstart"]),
        ("ap_prefix",      po["fstart"],      po["stream_start"]),
        ("ap_deflate",     po["stream_start"], po["stream_end"]),
        ("ap_size+crc16",  po["stream_end"],  po["fend"]),
        ("tail+trailer",   po["fend"],        len(orig)),
    ]
    out = []
    for name, a, b in regions:
        ob = orig[a:b]
        bb = built[a:b] if name not in ("tail+trailer",) else None
        if name == "tail+trailer":
            # tail may have shifted if the AP size changed; compare by slicing from the built end
            bb = built[len(built) - (len(orig) - a):] if len(built) >= (len(orig) - a) else b""
        same = ob == bb
        note = "identical" if same else f"DIFFERS ({len(ob)}B orig)"
        out.append((name, same, note))
    sized = "same size" if len(orig) == len(built) else f"size {len(orig):#x} -> {len(built):#x}"
    return out, sized


def _check(m, label=""):
    p = parse(m)
    calc16 = binascii.crc_hqx(m[p["fstart"]:p["stream_end"]], 0xFFFF)
    stored32 = struct.unpack_from(">I", m, len(m) - 4)[0]
    calc32 = crc32_msb(m[:-4])
    hdrcrc_stored = struct.unpack_from("<I", m, HDR + H_HDRCRC * 4)[0]
    hdrcrc_calc = crc32_msb(m[p["fstart"]:p["fend"]])
    filesz_stored = struct.unpack_from("<I", m, HDR + H_FILESZ * 4)[0]
    ok = (calc16 == p["crc16"] and calc32 == stored32 and hdrcrc_calc == hdrcrc_stored
          and filesz_stored == len(m) - 4)
    if label:
        print(f"[{label}] ap_crc16 {calc16:#06x}/{p['crc16']:#06x}  "
              f"hdr_crc32 {hdrcrc_calc:#010x}/{hdrcrc_stored:#010x}  "
              f"file_crc32 {calc32:#010x}/{stored32:#010x}  "
              f"filesz-4 {filesz_stored:#x}/{len(m) - 4:#x}  "
              f"{'ALL OK' if ok else 'MISMATCH'}")
    return ok


def main():
    if sys.argv[1] == "roundtrip":
        src = Path(sys.argv[2])
        m = src.read_bytes()
        _check(m, "input")
        built = repack(m, None)
        _check(built, "passthrough")
        if built == m:
            print(f"ROUND-TRIP OK: passthrough repack is byte-identical ({len(m):#x} bytes)")
            sys.exit(0)
        # locate first difference for diagnosis
        n = min(len(m), len(built))
        first = next((i for i in range(n) if m[i] != built[i]), n)
        diffs, sized = region_diff(m, built)
        print(f"ROUND-TRIP FAILED: first diff at {first:#x}; {sized}")
        for name, same, note in diffs:
            print(f"    {name:16s} {note}")
        sys.exit(1)

    if sys.argv[1] == "repack":
        orig = Path(sys.argv[2]).read_bytes()
        new_ap = Path(sys.argv[3]).read_bytes()
        out = Path(sys.argv[4])
        built = repack(orig, new_ap)
        _check(built, "repacked")
        diffs, sized = region_diff(orig, built)
        print(f"diff vs original: {sized}")
        for name, same, note in diffs:
            print(f"    {name:16s} {note}")
        out.write_bytes(built)
        print(f"wrote {out} ({len(built):#x} bytes)")
        sys.exit(0)

    # default: inspect (+ optional unpack)
    src = Path(sys.argv[1])
    m = src.read_bytes()
    p = parse(m)
    print(f"AP blob  flash {p['fstart']:#x}-{p['fend']:#x}  ram {p['rstart']:#x}-{p['rend']:#x}")
    print(f"deflate  {p['stream_start']:#x}-{p['stream_end']:#x} -> {len(p['ap']):#x} bytes "
          f"(stored size {p['size']:#x})")
    _check(m, "checks")
    if len(sys.argv) > 2:
        out = Path(sys.argv[2])
        out.mkdir(parents=True, exist_ok=True)
        (out / "pm_8051.bin").write_bytes(m[:HDR])
        (out / "sboot.bin").write_bytes(m[HDR:p["fstart"]])
        (out / "ap_prefix.bin").write_bytes(m[p["fstart"]:p["stream_start"]])
        (out / "ap_decompressed.bin").write_bytes(p["ap"])
        (out / "tail_resources.bin").write_bytes(m[p["fend"]:])


if __name__ == "__main__":
    main()
