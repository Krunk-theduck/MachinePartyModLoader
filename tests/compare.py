"""Cross-check the C++ patcher against the original Python installer.

usage: python3 compare.py <path to MachinePartyModLoader binary> <path to modloader_patch.py>
"""
import hashlib, importlib.util, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import make_test_pck as gen

loader, pyscript = Path(sys.argv[1]).resolve(), sys.argv[2]
spec = importlib.util.spec_from_file_location("mp", pyscript)
mp = importlib.util.module_from_spec(spec); spec.loader.exec_module(mp)

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def contents(pck):
    p = mp.Pck(pck)
    return {e.path: p.read_file(e.path) for e in p.entries}, p
fails = 0
def check(cond, msg):
    global fails
    print(("  ok   " if cond else "  FAIL ") + msg)
    fails += 0 if cond else 1

def run(cwd): return subprocess.run([str(cwd / "MachinePartyModLoader")], cwd=cwd, capture_output=True, text=True)

bootstrap = mp.BOOTSTRAP
for name, fmt, rel in [("fmt3", 3, False), ("fmt3+rel", 3, True), ("fmt2", 2, False)]:
    print(f"== {name}")
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        ref = t / "ref" ; ref.mkdir(); game = t / "game"; game.mkdir()
        gen.build(ref / "g.pck", fmt, rel); shutil.copy(ref / "g.pck", game / "Game.pck")
        (game / "Game").write_bytes(b"#!/bin/sh\n"); (game / "Game").chmod(0o755)
        shutil.copy(loader, game / "MachinePartyModLoader")
        original_hash = sha(game / "Game.pck")

        # reference: Python installer, patched in place on its own copy
        mp.say = lambda m: None
        mp.patch(ref / "g.pck", bootstrap)
        ref_files, _ = contents(ref / "g.pck")

        r = run(game)
        check(r.returncode == 0, f"C++ standalone run exit 0 {r.stderr.strip()[:200]}")
        modded = game / "Game.modded.pck"
        check(modded.exists(), "modded pck written")
        check(sha(game / "Game.pck") == original_hash, "original pck untouched")
        if not modded.exists(): continue
        got_files, gp = contents(modded)
        check(set(got_files) == set(ref_files), "same set of files as Python installer")
        for k in ref_files:
            if k == "res://modloader/.stamp": continue
            check(got_files.get(k) == ref_files[k], f"identical content: {k}")
        for e in gp.entries:  # md5s in the index must match the data
            check(hashlib.md5(got_files[e.path]).digest() == e.md5, f"index md5 ok: {e.path}") if e.path.startswith("res://modloader") or e.path.endswith("project.binary") else None
        check((game / "mods" / "console" / "main.gd").is_file(), "example mod written")

        # second run: nothing to do, modded untouched
        m1 = modded.stat().st_mtime_ns
        r = run(game)
        check("nothing to do" in r.stderr and modded.stat().st_mtime_ns == m1, "2nd run is a no-op")

        # Steam touches the timestamp but content is identical: hash path, no rebuild
        os.utime(game / "Game.pck", (time.time() + 100, time.time() + 100))
        r = run(game)
        check("nothing to do" in r.stderr and modded.stat().st_mtime_ns == m1, "touched mtime -> hash match -> no rebuild")

        # game update: different content -> rebuild with new data
        gen.build(game / "Game.pck", fmt, rel, seed=9)
        r = run(game)
        new_files, _ = contents(modded)
        check("rebuilding" in r.stderr, "game update detected, rebuilt")
        check(new_files["res://scripts/game_manager.gd"] == gen.files(9)["res://scripts/game_manager.gd"], "rebuilt pck carries the NEW game files")
        check(new_files["res://modloader/bootstrap.gd"] == bootstrap.encode(), "bootstrap present after rebuild")

        # launch mode with args: fake game records its argv
        (game / "Game").write_text("#!/bin/sh\nprintf '%s\\n' \"$@\" > args.txt\n")
        r = subprocess.run([str(game / "MachinePartyModLoader"), str(game / "Game"), "--foo", "bar baz"], cwd=t, capture_output=True, text=True)
        a = (t / "args.txt").read_text().splitlines() if (t / "args.txt").exists() else []
        check(a == ["--foo", "bar baz", "--main-pack", str(modded)], f"launch mode passes args + --main-pack ({a})")

        # uninstall
        subprocess.run([str(game / "MachinePartyModLoader"), "--uninstall"], cwd=game, capture_output=True)
        check(not modded.exists() and not (game / "Game.modded.pck.record").exists(), "--uninstall removes modded pck + record")
        check(sha(game / "Game.pck") != "" and (game / "Game.pck").exists(), "original still there")

        # broken pck -> still launches vanilla
        (game / "Game.pck").write_bytes(b"not a pck at all" * 10)
        r = subprocess.run([str(game / "MachinePartyModLoader"), str(game / "Game"), "--x"], cwd=t, capture_output=True, text=True)
        a = (t / "args.txt").read_text().splitlines()
        check(a == ["--x"], f"patch failure falls back to vanilla launch ({a})")

print("\nFAILED" if fails else "\nALL PASSED"); sys.exit(1 if fails else 0)
