#!/usr/bin/env python3
"""Inspect / unpack the MStar MST9U scaler image (Projector FW/MERGE.bin).

Known layout (M0R005):
  0x000000  8051 PM (standby) code
  0x010000  header, u32 LE: ap_flash_start, ap_ram_start, ap_ram_end, ap_flash_end,
            0, <unknown 0x1b41bbca>, 0, 0, file_size-4, 0x10000
  0x010000+ sboot (loader, contains zlib inflate)
  0x020080  AP blob: 0x3800 bytes uncompressed (exception vectors, image info header at
            +0x1100, resource table "TEL" at +0x2000), then a raw deflate stream
            (0x23880), then u32 LE decompressed size, then CRC-16/CCITT-FALSE (LE)
            over [ap_flash_start, end of deflate stream)
  0x0a9d00+ resources referenced by the TEL table (JPEG logo at 0xb9d08, ...)
  EOF-12    55 AA AB CD magic, EOF-4: CRC-32 (poly 04C11DB7, MSB first, init 0,
            no final xor) over everything before it, stored big-endian
"""
import binascii
import struct
import sys
import zlib
from pathlib import Path

HDR = 0x10000
AP_PREFIX = 0x3800  # uncompressed head of the AP blob


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


def main():
    src = Path(sys.argv[1])
    m = src.read_bytes()
    p = parse(m)
    print(f"AP blob  flash {p['fstart']:#x}-{p['fend']:#x}  ram {p['rstart']:#x}-{p['rend']:#x}")
    print(f"deflate  {p['stream_start']:#x}-{p['stream_end']:#x} -> {len(p['ap']):#x} bytes "
          f"(stored size {p['size']:#x})")
    calc16 = binascii.crc_hqx(m[p["fstart"]:p["stream_end"]], 0xFFFF)
    print(f"AP crc16 stored {p['crc16']:#06x} calc {calc16:#06x} {'OK' if calc16 == p['crc16'] else 'BAD'}")
    stored32 = struct.unpack_from(">I", m, len(m) - 4)[0]
    calc32 = crc32_msb(m[:-4])
    print(f"file crc32 stored {stored32:#010x} calc {calc32:#010x} {'OK' if calc32 == stored32 else 'BAD'}")
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
