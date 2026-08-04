#!/usr/bin/env python3
"""
pck_probe2.py — dump and decode the trailing file index of a format-3 .pck.

Usage:  python pck_probe2.py "Machine Party.pck"

Read only. Never writes. Paste the whole output back.
"""

import struct
import sys
from pathlib import Path

OFF_FLAGS = 20
OFF_FILE_BASE = 24
OFF_DIR_OFFSET = 32
MAX_ENTRIES = 400_000


def hexdump(b, base=0, width=16):
    out = []
    for i in range(0, len(b), width):
        c = b[i : i + width]
        hx = " ".join(f"{x:02x}" for x in c).ljust(width * 3 - 1)
        asc = "".join(chr(x) if 32 <= x < 127 else "." for x in c)
        out.append(f"  {base+i:08x}  {hx}  |{asc}|")
    return "\n".join(out)


def printable_ratio(b):
    if not b:
        return 0.0
    return sum(1 for x in b if 32 <= x < 127 or x == 0) / len(b)


def walk(region, start, filesize, file_base, rel, has_flags, require_res):
    """Walk an entry chain. Return info if it lands exactly on the end."""
    pos = start
    n = 0
    paths = []
    while pos < len(region):
        if n > MAX_ENTRIES:
            return None
        if pos + 4 > len(region):
            return None
        (plen,) = struct.unpack_from("<I", region, pos)
        pos += 4
        if not (1 <= plen <= 4096):
            return None
        need = plen + 16 + 16 + (4 if has_flags else 0)
        if pos + need > len(region):
            return None
        raw = region[pos : pos + plen]
        pos += plen
        try:
            p = raw.rstrip(b"\x00").decode("utf-8")
        except UnicodeDecodeError:
            return None
        if not p or any(ord(c) < 32 for c in p):
            return None
        if require_res and not p.startswith("res://"):
            return None
        off, size = struct.unpack_from("<QQ", region, pos)
        pos += 32  # offset+size+md5
        if has_flags:
            pos += 4
        abs_off = off + (file_base if rel else 0)
        if size > filesize or abs_off + size > filesize:
            return None
        if len(paths) < 10:
            paths.append(p)
        n += 1
        if pos == len(region):
            return {"count": n, "paths": paths, "start": start,
                    "has_flags": has_flags, "require_res": require_res}
    return None


def main():
    pck = Path(sys.argv[1]) if len(sys.argv) > 1 else next(
        iter(sorted(Path(__file__).parent.glob("*.pck"))), None)
    if pck is None:
        print("no .pck found")
        return

    filesize = pck.stat().st_size
    with open(pck, "rb") as f:
        head = f.read(256)
        (flags,) = struct.unpack_from("<I", head, OFF_FLAGS)
        (file_base,) = struct.unpack_from("<Q", head, OFF_FILE_BASE)
        (dir_raw,) = struct.unpack_from("<Q", head, OFF_DIR_OFFSET)
        rel = bool(flags & 2)

        print(f"file        : {pck.name}")
        print(f"size        : {filesize:,}")
        print(f"flags       : {flags}   rel_filebase={rel}")
        print(f"file_base   : {file_base:,}  (0x{file_base:x})")
        print(f"dir pointer : {dir_raw:,}  (0x{dir_raw:x})")
        print(f"tail length : {filesize - dir_raw:,} bytes")
        print()

        for label, target in (("dir_raw", dir_raw), ("dir_raw+file_base", dir_raw + file_base)):
            if not (0 < target < filesize):
                continue
            f.seek(max(0, target - 32))
            print(f"bytes around {label} = 0x{target:x}:")
            print(hexdump(f.read(480), base=max(0, target - 32)))
            print()

        start = min(dir_raw, dir_raw + file_base)
        f.seek(start)
        region = f.read(filesize - start)

    print(f"printable/zero ratio of tail: {printable_ratio(region):.3f} "
          f"(low means compressed or encrypted)")
    print()

    print("brute-forcing the entry table start...")
    results = []
    for has_flags in (True, False):
        for require_res in (True, False):
            for delta in range(0, 4096, 4):
                r = walk(region, delta, filesize, file_base, rel, has_flags, require_res)
                if r:
                    r["abs_start"] = start + delta
                    r["delta"] = delta
                    results.append(r)
                    break
            if results and results[-1].get("has_flags") == has_flags and results[-1].get("require_res") == require_res:
                continue

    if not results:
        print("  no entry chain found that ends exactly at EOF.")
        print("  the index is probably compressed — paste the hexdumps above.")
        return

    seen = set()
    for r in results:
        key = (r["abs_start"], r["has_flags"])
        if key in seen:
            continue
        seen.add(key)
        print()
        print(f"  entries start at 0x{r['abs_start']:x}  "
              f"({r['delta']} bytes past the pointer)")
        print(f"  entry count    : {r['count']:,}")
        print(f"  trailing flags : {r['has_flags']}")
        print(f"  paths keep the res:// prefix : {r['paths'][0].startswith('res://')}")
        if r["delta"] >= 4:
            preamble = region[: r["delta"]]
            print(f"  preamble bytes : {preamble[:32].hex(' ')}")
            if r["delta"] >= 4:
                print(f"  preamble as u32: "
                      f"{struct.unpack_from('<I', preamble, 0)[0]:,}")
        print("  first paths:")
        for p in r["paths"]:
            print(f"    {p}")


main()
