#!/usr/bin/env python3
"""
pck_probe.py — figure out the exact header layout of a Godot .pck.

Usage:  python pck_probe.py "Machine Party.pck"
        python pck_probe.py            (auto-finds a .pck next to this file)

Reads only. Never writes. Paste the whole output back.
"""

import struct
import sys
from pathlib import Path

MAX_SCAN = 512          # how far into the header to look for file_count
SAMPLE_ENTRIES = 2000   # validate at most this many entries per candidate


def hexdump(b, base=0, width=16):
    out = []
    for i in range(0, len(b), width):
        chunk = b[i : i + width]
        hx = " ".join(f"{c:02x}" for c in chunk).ljust(width * 3 - 1)
        asc = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
        out.append(f"  {base+i:04x}  {hx}  |{asc}|")
    return "\n".join(out)


def try_layout(buf, filesize, count_off, extra_after_md5, pad_path):
    """Walk the entry table under one candidate layout. Return info or None."""
    if count_off + 4 > len(buf):
        return None
    (count,) = struct.unpack_from("<I", buf, count_off)
    if not (1 <= count <= 2_000_000):
        return None

    pos = count_off + 4
    paths, offsets = [], []
    limit = min(count, SAMPLE_ENTRIES)

    for i in range(limit):
        if pos + 4 > len(buf):
            return None
        (plen,) = struct.unpack_from("<I", buf, pos)
        pos += 4
        if not (4 <= plen <= 4096):
            return None
        if pad_path and plen % 4 != 0:
            return None
        if pos + plen + 16 + 16 + extra_after_md5 > len(buf):
            return None
        raw = buf[pos : pos + plen]
        pos += plen
        try:
            p = raw.rstrip(b"\x00").decode("utf-8")
        except UnicodeDecodeError:
            return None
        if not p.startswith("res://"):
            return None
        off, size = struct.unpack_from("<QQ", buf, pos)
        pos += 16
        pos += 16  # md5
        pos += extra_after_md5
        if size > filesize or off > filesize:
            return None
        if i < 8:
            paths.append(p)
        offsets.append(off)

    return {
        "count": count,
        "count_off": count_off,
        "extra_after_md5": extra_after_md5,
        "sampled": limit,
        "index_end_sampled": pos,
        "paths": paths,
        "min_off": min(offsets),
        "max_off_end": max(offsets),
    }


def main():
    if len(sys.argv) > 1:
        pck = Path(sys.argv[1])
    else:
        found = sorted(Path(__file__).parent.glob("*.pck"))
        if not found:
            print("no .pck found — pass one as an argument")
            return
        pck = found[0]

    filesize = pck.stat().st_size
    # Entry tables can be large; read generously.
    with open(pck, "rb") as f:
        buf = f.read(min(filesize, 64 * 1024 * 1024))

    print(f"file      : {pck.name}")
    print(f"size      : {filesize:,} bytes")
    print(f"magic     : {buf[:4]!r}")
    if len(buf) >= 20:
        fmt, maj, mnr, pat = struct.unpack_from("<IIII", buf, 4)
        print(f"pack fmt  : {fmt}")
        print(f"godot ver : {maj}.{mnr}.{pat}")
    print()
    print("first 320 bytes:")
    print(hexdump(buf[:320]))
    print()
    print(f"bytes at 0x{filesize-64:x} (last 64):")
    with open(pck, "rb") as f:
        f.seek(max(0, filesize - 64))
        print(hexdump(f.read(64), base=max(0, filesize - 64)))
    print()

    print("scanning for the entry table...")
    hits = []
    for count_off in range(8, MAX_SCAN, 4):
        for extra in (0, 4, 8, 12):
            for pad in (True, False):
                r = try_layout(buf, filesize, count_off, extra, pad)
                if r:
                    r["pad_path"] = pad
                    hits.append(r)
                    break
            if hits and hits[-1]["count_off"] == count_off:
                break

    if not hits:
        print("  NO LAYOUT FOUND — the directory may be compressed or encrypted.")
        return

    for r in hits[:4]:
        print()
        print(f"  count_off        = {r['count_off']}  (0x{r['count_off']:x})")
        print(f"  file_count       = {r['count']:,}")
        print(f"  extra_after_md5  = {r['extra_after_md5']}")
        print(f"  path 4-byte pad  = {r['pad_path']}")
        print(f"  validated        = {r['sampled']:,} entries")
        print(f"  smallest offset  = {r['min_off']:,}")
        print(f"  first paths:")
        for p in r["paths"]:
            print(f"    {p}")
    print()
    print(f"({len(hits)} candidate layout(s) total)")


main()
