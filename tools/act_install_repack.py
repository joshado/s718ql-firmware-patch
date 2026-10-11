#!/usr/bin/env python3
"""Repack the Actiontec (Realtek RTD1295) install.img.

install.img format (reverse-engineered 2026-10-11):
  [ GNU tar archive, padded to a 512-byte block boundary, ending at
    offset TAR_BODY_LEN ]
  [ 32 ASCII-hex bytes = md5( bytes[0 : TAR_BODY_LEN] ) ]

The trailing MD5 is the package integrity gate (secure_boot=0 / signature=""
in config.txt mean there is no RSA/efuse signature check). Because we splice
the (possibly modified) rootfs.ext4.bin in place at its exact tar data offset
and recompute only the MD5 trailer, every other byte is identical to the
source. Splicing the UNMODIFIED rootfs reproduces the source byte-identically
(round-trip proof).

The constants below were measured from the m0r005 package's install.img
(177,213,472 bytes). If a different package is used, re-measure with a tar
header walk (see tools notes / docs/firmware-components.md section 4).

Usage:
  act_install_repack.py <src_install.img> <rootfs.ext4.bin> <out_install.img>

The replacement rootfs.ext4.bin MUST be exactly ROOTFS_SIZE bytes (edit the
ext4 in place via loopback/debugfs; do not resize) or the tar headers that
follow would be corrupted.
"""
import sys, hashlib

ROOTFS_DATA_OFF = 12366848      # tar data offset of generic/rootfs.ext4.bin
ROOTFS_SIZE     = 112051200     # its size, as recorded in the tar header
TAR_BODY_LEN    = 177213440     # tar padded length; md5 covers [0:TAR_BODY_LEN)
TOTAL_LEN       = 177213472     # TAR_BODY_LEN + 32 (ascii-hex md5 trailer)


def repack(src, rootfs, out):
    with open(src, 'rb') as f:
        data = bytearray(f.read())
    if len(data) != TOTAL_LEN:
        sys.exit(f"src len {len(data)} != expected {TOTAL_LEN}")
    with open(rootfs, 'rb') as f:
        new = f.read()
    if len(new) != ROOTFS_SIZE:
        sys.exit(f"rootfs len {len(new)} != {ROOTFS_SIZE} (must not resize)")
    data[ROOTFS_DATA_OFF:ROOTFS_DATA_OFF + ROOTFS_SIZE] = new
    digest = hashlib.md5(bytes(data[:TAR_BODY_LEN])).hexdigest()
    data[TAR_BODY_LEN:TOTAL_LEN] = digest.encode('ascii')
    with open(out, 'wb') as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes)")
    print(f"md5 trailer = {digest}")
    print(f"sha256      = {hashlib.sha256(bytes(data)).hexdigest()}")


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    repack(sys.argv[1], sys.argv[2], sys.argv[3])
