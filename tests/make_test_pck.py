"""Build small synthetic Godot-style .pck files (format 2 and 3) for testing."""
import hashlib, os, struct, sys

def pad4(b): return b + b"\0" * ((4 - len(b) % 4) % 4)

def enc_string(s):
    b = s.encode(); out = struct.pack("<II", 4, len(b)) + b
    return out + b"\0" * ((4 - len(out) % 4) % 4)

def project_binary():
    settings = [("application/config/name", enc_string("Machine Party")),
                ("autoload/GameManager", enc_string("*res://scripts/game_manager.gd")),
                ("autoload/NetworkManager", enc_string("*res://scripts/network_manager.gd"))]
    out = b"ECFG" + struct.pack("<I", len(settings))
    for k, v in settings:
        kb = k.encode(); out += struct.pack("<I", len(kb)) + kb + struct.pack("<I", len(v)) + v
    return out

def files(seed=0):
    rnd = lambda n, s: hashlib.sha256(bytes([s])).digest() * n
    return {"res://project.binary": project_binary(),
            "res://scripts/game_manager.gd": b"extends Node\n" + rnd(7, seed),
            "res://scenes/main_menu/main_menu.tscn": rnd(40, seed + 1),
            "res://art/logo.png": rnd(300, seed + 2),
            "res://scripts/odd.gd": b"abc"}  # odd size on purpose

def build(path, fmt, rel=False, seed=0):
    fs = files(seed)
    if fmt == 3:
        header_size = 4 + 4 + 12 + 4 + 8 + 8 + 64
    else:
        header_size = 96
    file_base = header_size if rel else 0
    # lay out file data right after the header (fmt3: dir at end; fmt2: dir first)
    entries, blobs = [], []
    if fmt == 3:
        cursor = header_size
        for p, d in fs.items():
            entries.append((p, cursor, len(d), hashlib.md5(d).digest())); blobs.append(d); cursor += len(d)
        dir_abs = cursor
    else:
        index_size = 4 + sum(4 + len(pad4(p.encode())) + 8 + 8 + 16 + 4 for p in fs)
        cursor = header_size + index_size
        for p, d in fs.items():
            entries.append((p, cursor, len(d), hashlib.md5(d).digest())); blobs.append(d); cursor += len(d)
    index = struct.pack("<I", len(entries))
    for p, off, size, md5 in entries:
        b = pad4(p.encode())
        stored = off - file_base
        index += struct.pack("<I", len(b)) + b + struct.pack("<QQ", stored, size) + md5 + struct.pack("<I", 0)
    flags = 2 if rel else 0
    if fmt == 3:
        hdr = b"GDPC" + struct.pack("<I", 3) + struct.pack("<III", 4, 4, 0) + struct.pack("<I", flags)
        hdr += struct.pack("<Q", file_base) + struct.pack("<Q", dir_abs - file_base) + b"\0" * 64
        data = hdr + b"".join(blobs) + index
    else:
        hdr = b"GDPC" + struct.pack("<I", 2) + struct.pack("<III", 3, 5, 0) + struct.pack("<I", flags)
        hdr += struct.pack("<Q", file_base) + b"\0" * 64
        data = hdr + index + b"".join(blobs)
    assert len(hdr) == header_size, (len(hdr), header_size)
    open(path, "wb").write(data)

if __name__ == "__main__":
    build(sys.argv[1], int(sys.argv[2]), rel=(len(sys.argv) > 3 and sys.argv[3] == "rel"))
