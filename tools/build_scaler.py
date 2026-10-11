#!/usr/bin/env python3
"""Build the patched scaler MERGE.bin from git-tracked patch definitions.

Applies feature a (no-signal background -> red): a code-cave hook at
osd_nosig_draw (0x1dbc84) that writes the free-run colour register. The assembled
bytes come from tools/aeon-ghidra/patches/feature-a-nosig-red.asm.txt (produced by
AeonAssemble.java from the .lst; re-run that only if the .lst changes).

Safety: asserts the hook-site original bytes and that the cave is empty before
patching; the repacker recomputes all CRCs and fixes up TEL offsets.

  build_scaler.py <orig MERGE.bin> <asm.txt> <out MERGE.bin> [data_patch.txt ...]

Each data_patch line: `RAM_addr expect_hex replace_hex` (RAM = 0x183800 + dec
offset for AP-stream bytes). expect_hex guards against drift; same length as
replace_hex keeps the edit size-neutral (e.g. feature b, the unplg typo reword).
"""
import sys, hashlib, importlib.util
from pathlib import Path

mm_spec = importlib.util.spec_from_file_location("mm", str(Path(__file__).with_name("mst9u_merge.py")))
mm = importlib.util.module_from_spec(mm_spec); mm_spec.loader.exec_module(mm)

BASE, DEC = 0x180000, 0x183800
HOOK_RAM = 0x1dbc84
HOOK_ORIG = bytes.fromhex("9c380d21040d4100")   # the 3 displaced prologue instrs (8 B)


def load_asm(path):
    out = []
    for ln in Path(path).read_text().splitlines():
        ln = ln.strip()
        if not ln or ln.startswith(";"):
            continue
        a, h = ln.split()[0], ln.split()[1]
        out.append((int(a, 16), bytes.fromhex(h)))
    return out


def load_data_patches(paths):
    out = []
    for path in paths:
        for ln in Path(path).read_text().splitlines():
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            addr, exp, rep = ln.split()
            out.append((int(addr, 16), bytes.fromhex(exp), bytes.fromhex(rep)))
    return out


def build(orig, asm, data_patches=()):
    p = mm.parse(orig)
    prefix = bytearray(orig[p["fstart"]:p["stream_start"]])
    ap = bytearray(p["ap"])
    assert bytes(ap[HOOK_RAM - DEC:HOOK_RAM - DEC + 8]) == HOOK_ORIG, "hook-site bytes changed"
    cave = [(a, b) for a, b in asm if a < DEC]
    lo = min(a for a, _ in cave); hi = max(a + len(b) for a, b in cave)
    assert all(x == 0 for x in prefix[lo - BASE:hi - BASE]), "cave region not empty"
    for addr, b in asm:
        if addr < DEC:
            prefix[addr - BASE:addr - BASE + len(b)] = b
        else:
            ap[addr - DEC:addr - DEC + len(b)] = b
    for addr, exp, rep in data_patches:
        assert addr >= DEC, "data patches target the AP stream"
        off = addr - DEC
        assert len(exp) == len(rep), "data patch must be size-neutral"
        assert bytes(ap[off:off + len(exp)]) == exp, f"data patch @ {addr:#x}: expect mismatch"
        ap[off:off + len(rep)] = rep
    built = mm.repack(orig, new_ap=bytes(ap), new_prefix=bytes(prefix))
    assert mm._check(built), "patched image failed self-check"
    return built


def main():
    orig = Path(sys.argv[1]).read_bytes()
    asm = load_asm(sys.argv[2])
    data_patches = load_data_patches(sys.argv[4:])
    built = build(orig, asm, data_patches)
    out = Path(sys.argv[3]); out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(built)
    print(f"wrote {out} ({len(built):#x} B)")
    print(f"  src  sha256 {hashlib.sha256(orig).hexdigest()}")
    print(f"  out  sha256 {hashlib.sha256(built).hexdigest()}")
    for name, _, note in mm.region_diff(orig, built)[0]:
        print(f"  {name:16s} {note}")


if __name__ == "__main__":
    main()
