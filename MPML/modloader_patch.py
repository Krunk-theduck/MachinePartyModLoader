
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

PCK_MAGIC = b"GDPC"
PACK_DIR_ENCRYPTED = 1 << 0
PACK_REL_FILEBASE = 1 << 1

OFF_FORMAT = 4
OFF_VERSION = 8
OFF_FLAGS = 20
OFF_FILE_BASE = 24
OFF_DIR_OFFSET = 32
V2_INDEX_START = 100

LOADER_VERSION = "2"
BOOTSTRAP_RES = "res://modloader/bootstrap.gd"
STAMP_RES = "res://modloader/.stamp"
AUTOLOAD_KEY = "autoload/ModLoader"
AUTOLOAD_VALUE = "*" + BOOTSTRAP_RES
PROJECT_BINARY = "res://project.binary"

CHUNK = 1 << 20

class Entry:
    __slots__ = ("path", "offset", "size", "md5", "flags", "data")

    def __init__(self, path, offset, size, md5, flags, data=None):
        self.path = path
        self.offset = offset
        self.size = size
        self.md5 = md5
        self.flags = flags
        self.data = data

def pad4(b: bytes) -> bytes:
    return b + b"\x00" * ((4 - len(b) % 4) % 4)

class Pck:
    def __init__(self, path: Path):
        self.path = Path(path)
        self.filesize = self.path.stat().st_size
        self.entries: list[Entry] = []
        self.fmt = 2
        self.ver = (0, 0, 0)
        self.flags = 0
        self.file_base = 0
        self.dir_offset = 0
        self.dir_field = None
        self.dir_field_base = 0
        self.dir_has_count = True
        self.res_prefix = True
        self._read()

    @property
    def rel(self) -> bool:
        return bool(self.flags & PACK_REL_FILEBASE)

    def norm(self, path: str) -> str:
        """Match a canonical res://... path to how this pck actually stores
        it — some builds store the full res:// path, others store it bare."""
        has_prefix = path.startswith("res://")
        if self.res_prefix and not has_prefix:
            return "res://" + path
        if not self.res_prefix and has_prefix:
            return path[len("res://"):]
        return path

    def _read(self):
        with open(self.path, "rb") as f:
            head = f.read(256)
            if head[:4] != PCK_MAGIC:
                die(
                    f"{self.path.name} does not start with GDPC — it is probably "
                    "embedded inside the .exe, which this tool cannot patch."
                )
            (self.fmt,) = struct.unpack_from("<I", head, OFF_FORMAT)
            self.ver = struct.unpack_from("<III", head, OFF_VERSION)
            if self.fmt >= 2:
                (self.flags,) = struct.unpack_from("<I", head, OFF_FLAGS)
                (self.file_base,) = struct.unpack_from("<Q", head, OFF_FILE_BASE)
            if self.flags & PACK_DIR_ENCRYPTED:
                die("This .pck has an encrypted directory. Cannot patch.")

            if self.fmt >= 3:
                self._locate_directory_v3(f, head)
            else:
                self.dir_offset = V2_INDEX_START - 4
                self.entries = self._parse_directory(f, V2_INDEX_START - 4, True)
                if self.entries is None:
                    die("could not parse the format-2 file index.")

            if self.entries:
                self.res_prefix = self.entries[0].path.startswith("res://")

    def _locate_directory_v3(self, f, head):

        cands = []
        for field in [OFF_DIR_OFFSET] + list(range(24, 112, 8)):
            if field + 8 > len(head) or field in (c[0] for c in cands):
                continue
            (raw,) = struct.unpack_from("<Q", head, field)
            for base in (0, self.file_base):
                target = raw + base
                if self.file_base < target < self.filesize:
                    cands.append((field, base, target))

        for field, base, target in cands:
            for has_count in (True, False):
                entries = self._parse_directory(f, target, has_count)
                if entries:
                    self.dir_field = field
                    self.dir_field_base = base
                    self.dir_offset = target
                    self.dir_has_count = has_count
                    self.entries = entries
                    return
        die(
            "Could not locate the file index. The header looks like pack format "
            f"{self.fmt}, which this tool does not fully understand."
        )

    def _parse_directory(self, f, start, has_count):

        f.seek(start)
        region = f.read(self.filesize - start)
        pos = 0
        try:
            if has_count is not False:
                (count,) = struct.unpack_from("<I", region, 0)
                pos = 4
                if not (1 <= count <= 5_000_000):
                    return None
            else:
                count = None

            entries = []
            i = 0
            while True:
                if count is not None and i >= count:
                    break
                if pos >= len(region):
                    break
                (plen,) = struct.unpack_from("<I", region, pos)
                pos += 4
                if not (4 <= plen <= 4096) or plen % 4 != 0:
                    return None
                raw = region[pos : pos + plen]
                pos += plen
                p = raw.rstrip(b"\x00").decode("utf-8")
                if not p:
                    return None
                off, size = struct.unpack_from("<QQ", region, pos)
                pos += 16
                md5 = region[pos : pos + 16]
                pos += 16
                eflags = 0
                if self.fmt >= 2:
                    (eflags,) = struct.unpack_from("<I", region, pos)
                    pos += 4
                abs_off = off + (self.file_base if self.rel else 0)
                if abs_off + size > self.filesize:
                    return None
                entries.append(Entry(p, abs_off, size, md5, eflags))
                i += 1
        except (struct.error, UnicodeDecodeError, IndexError):
            return None

        if not entries:
            return None

        if self.fmt >= 3 and pos != len(region):
            return None
        return entries

    def get(self, res_path):
        res_path = self.norm(res_path)
        for e in self.entries:
            if e.path == res_path:
                return e
        return None

    def read_file(self, res_path):
        e = self.get(res_path)
        if e is None:
            return None
        if e.data is not None:
            return e.data
        with open(self.path, "rb") as f:
            f.seek(e.offset)
            return f.read(e.size)

    def put(self, res_path, data: bytes):
        res_path = self.norm(res_path)
        e = self.get(res_path)
        if e is None:
            e = Entry(res_path, 0, len(data), b"\x00" * 16, 0, data)
            self.entries.append(e)
        else:
            e.data = data
            e.size = len(data)
        e.md5 = hashlib.md5(data).digest()

    def _encode_index(self, entries) -> bytes:
        out = bytearray()
        if self.dir_has_count or self.fmt < 3:
            out += struct.pack("<I", len(entries))
        for e in entries:
            b = pad4(e.path.encode("utf-8"))
            stored = e.offset - (self.file_base if self.rel else 0)
            out += struct.pack("<I", len(b)) + b
            out += struct.pack("<QQ", stored, e.size)
            out += e.md5
            if self.fmt >= 2:
                out += struct.pack("<I", e.flags)
        return bytes(out)

    def append_patch_v3(self):

        pending = [e for e in self.entries if e.data is not None]
        with open(self.path, "r+b") as f:
            f.seek(0, os.SEEK_END)
            for e in pending:
                e.offset = f.tell()
                f.write(e.data)

            new_dir = f.tell()
            f.write(self._encode_index(sorted(self.entries, key=lambda e: e.path)))
            f.flush()
            os.fsync(f.fileno())

            f.seek(self.dir_field)
            f.write(struct.pack("<Q", new_dir - self.dir_field_base))
            f.flush()
            os.fsync(f.fileno())
        return new_dir

    def rewrite_v2(self, out_path: Path):
        entries = sorted(self.entries, key=lambda e: e.path)
        encoded = [pad4(e.path.encode("utf-8")) for e in entries]
        index_size = 4 + sum(4 + len(b) + 8 + 8 + 16 + 4 for b in encoded)
        old = [e.offset for e in entries]

        cursor = V2_INDEX_START - 4 + index_size
        new_offsets = []
        for e in entries:
            new_offsets.append(cursor)
            cursor += e.size

        self.flags &= ~PACK_REL_FILEBASE
        self.file_base = 0
        for e, no in zip(entries, new_offsets):
            e.offset = no

        with open(out_path, "wb") as out:
            out.write(PCK_MAGIC)
            out.write(struct.pack("<I", 2))
            out.write(struct.pack("<III", *self.ver))
            out.write(struct.pack("<I", 0))
            out.write(struct.pack("<Q", 0))
            out.write(b"\x00" * (16 * 4))
            out.write(self._encode_index(entries))
            with open(self.path, "rb") as src:
                for e, o in zip(entries, old):
                    if e.data is not None:
                        out.write(e.data)
                        continue
                    src.seek(o)
                    left = e.size
                    while left:
                        buf = src.read(min(CHUNK, left))
                        if not buf:
                            die(f"unexpected EOF reading {e.path}")
                        out.write(buf)
                        left -= len(buf)

def ecfg_parse(data: bytes):
    if data[:4] != b"ECFG":
        die("project.binary has a bad header.")
    off = 4
    (count,) = struct.unpack_from("<I", data, off)
    off += 4
    out = []
    for _ in range(count):
        (klen,) = struct.unpack_from("<I", data, off)
        off += 4
        key = data[off : off + klen].decode("utf-8")
        off += klen
        (vlen,) = struct.unpack_from("<I", data, off)
        off += 4
        out.append((key, data[off : off + vlen]))
        off += vlen
    return out

def ecfg_build(entries):
    out = bytearray(b"ECFG") + struct.pack("<I", len(entries))
    for key, val in entries:
        kb = key.encode("utf-8")
        out += struct.pack("<I", len(kb)) + kb
        out += struct.pack("<I", len(val)) + val
    return bytes(out)

def enc_string(s: str) -> bytes:
    b = s.encode("utf-8")
    out = struct.pack("<I", 4) + struct.pack("<I", len(b)) + b
    return out + b"\x00" * ((4 - len(out) % 4) % 4)

def dec_string(v: bytes):
    if len(v) < 8:
        return None
    (vtype,) = struct.unpack_from("<I", v, 0)
    if vtype & 0xFFFF != 4:
        return None
    (length,) = struct.unpack_from("<I", v, 4)
    return v[8 : 8 + length].decode("utf-8", "replace")

def sanitize(name: str) -> str:
    return re.sub(r'[:/\\?*"|%<>]', "-", name).strip() or "Godot Project"

def user_data_dir(settings):
    name = sanitize(settings.get("application/config/name", "Godot Project"))
    custom = settings.get("application/config/custom_user_dir_name", "").strip()
    use_custom = settings.get("application/config/use_custom_user_dir", "") == "1"
    system = platform.system()
    if system == "Windows":
        base = Path(os.environ.get("APPDATA", Path.home() / "AppData/Roaming"))
    elif system == "Darwin":
        base = Path.home() / "Library/Application Support"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    if use_custom and custom:
        return base / sanitize(custom)
    return base / "Godot" / "app_userdata" / name

def sidecar_path(pck: Path) -> Path:
    return pck.with_name(pck.name + ".modloader.json")

def save_undo(pck: Path, vanilla_dir_offset, vanilla_size, dir_field, dir_field_base):
    sidecar_path(pck).write_text(
        json.dumps(
            {
                "loader_version": LOADER_VERSION,
                "vanilla_dir_offset": vanilla_dir_offset,
                "vanilla_size": vanilla_size,
                "dir_field": dir_field,
                "dir_field_base": dir_field_base,
                "patched_size": pck.stat().st_size,
            },
            indent=2,
        ),
        encoding="utf-8",
    )

def load_undo(pck: Path):
    sc = sidecar_path(pck)
    if not sc.is_file():
        return None
    try:
        d = json.loads(sc.read_text(encoding="utf-8"))
    except Exception:
        return None
    if d.get("patched_size") != pck.stat().st_size:
        return None
    return d

def apply_undo(pck: Path, undo) -> bool:
    with open(pck, "r+b") as f:
        f.seek(undo["dir_field"])
        f.write(struct.pack("<Q", undo["vanilla_dir_offset"] - undo["dir_field_base"]))
        f.flush()
        f.truncate(undo["vanilla_size"])
        os.fsync(f.fileno())
    sidecar_path(pck).unlink(missing_ok=True)
    return True

def patch(pck_path: Path, bootstrap_src: str, force=False):
    p = Pck(pck_path)
    say(f"pck: {pck_path.name}  (Godot {'.'.join(map(str, p.ver))}, "
        f"pack format {p.fmt}, {len(p.entries):,} files)")
    if p.fmt >= 3:
        say(f"index at 0x{p.dir_offset:x} via header field 0x{p.dir_field:x}"
            f"{' (+file_base)' if p.dir_field_base else ''}")

    stamp = p.read_file(STAMP_RES)
    current = f"{LOADER_VERSION}:{hashlib.sha256(bootstrap_src.encode()).hexdigest()[:16]}"
    if stamp is not None and stamp.decode("utf-8", "replace").strip() == current and not force:
        say("already patched and up to date — nothing to do")
        return p, False

    undo = load_undo(pck_path)
    if undo and stamp is not None:
        say("rolling back previous patch before re-applying")
        apply_undo(pck_path, undo)
        p = Pck(pck_path)

    pb = p.read_file(PROJECT_BINARY)
    if pb is None:
        die("no project.binary inside the pck — is this really a Godot game?")

    settings_entries = ecfg_parse(pb)
    kept = [(k, v) for k, v in settings_entries if k != AUTOLOAD_KEY]
    new_entries = [(AUTOLOAD_KEY, enc_string(AUTOLOAD_VALUE))] + kept
    autoloads = [k for k, _ in new_entries if k.startswith("autoload/")]
    say(f"autoload order: {', '.join(a.split('/', 1)[1] for a in autoloads[:6])}"
        + (" ..." if len(autoloads) > 6 else ""))

    vanilla_dir_offset = p.dir_offset
    vanilla_size = pck_path.stat().st_size

    p.put(PROJECT_BINARY, ecfg_build(new_entries))
    p.put(BOOTSTRAP_RES, bootstrap_src.encode("utf-8"))
    p.put(STAMP_RES, current.encode("utf-8"))

    if p.fmt >= 3:
        added = sum(len(e.data) for e in p.entries if e.data is not None)
        new_dir = p.append_patch_v3()
        say(f"appended {added:,} bytes, new index at 0x{new_dir:x}")
        save_undo(pck_path, vanilla_dir_offset, vanilla_size, p.dir_field, p.dir_field_base)
    else:
        backup = pck_path.with_suffix(pck_path.suffix + ".vanilla")
        if stamp is None:
            say(f"backing up clean pck -> {backup.name}")
            shutil.copy2(pck_path, backup)
        tmp = Path(tempfile.mkstemp(dir=str(pck_path.parent), suffix=".tmp")[1])
        try:
            p.rewrite_v2(tmp)
            Pck(tmp)
            os.replace(tmp, pck_path)
        except BaseException:
            tmp.unlink(missing_ok=True)
            raise

    check = Pck(pck_path)
    got = check.read_file(BOOTSTRAP_RES)
    if got is None or got.decode("utf-8", "replace") != bootstrap_src:
        die("verification failed after patching — run --restore")
    if [k for k, _ in ecfg_parse(check.read_file(PROJECT_BINARY))][0] != AUTOLOAD_KEY:
        die("verification failed: autoload is not first — run --restore")
    say(f"patched and verified ({len(check.entries):,} files)")
    return check, True

EXAMPLE_MOD_JSON = """{
  "id": "console",
  "name": "Debug Console",
  "version": "1.0.0",
  "entry": "main.gd",
  "priority": 0,
  "requires": []
}
"""

EXAMPLE_MOD_GD = '''extends Node

const TOGGLE_KEY := KEY_QUOTELEFT

var _loader
var _panel: PanelContainer
var _log: RichTextLabel
var _input: LineEdit
var _open := false
var _prev_mouse_mode := Input.MOUSE_MODE_VISIBLE

func _mod_init(loader) -> void:
	_loader = loader
	loader.note("console mod init")


func _mod_ready(loader) -> void:
	loader.note("console mod ready")
	_build_ui()
	_print("debug console ready — press ~ to toggle, 'help' for commands")

func _build_ui() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 50
	get_tree().root.add_child.call_deferred(layer)

	_panel = PanelContainer.new()
	_panel.anchor_left = 0
	_panel.anchor_right = 1
	_panel.anchor_top = 1
	_panel.anchor_bottom = 1
	_panel.offset_left = 12
	_panel.offset_right = -12
	_panel.offset_top = -300
	_panel.offset_bottom = -16
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0, 0, 0, 0.75)
	style.set_border_width_all(1)
	style.border_color = Color(0.4, 0.9, 0.4)
	_panel.add_theme_stylebox_override("panel", style)
	_panel.visible = false
	layer.add_child(_panel)

	var margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 10)
	_panel.add_child(margin)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 8)
	margin.add_child(vbox)

	_log = RichTextLabel.new()
	_log.bbcode_enabled = true
	_log.scroll_active = true
	_log.custom_minimum_size = Vector2(0, 200)
	_log.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(_log)

	_input = LineEdit.new()
	_input.placeholder_text = "type a command, 'help' to list them"
	_input.tooltip_text = "commands: help, mods, note <text>, has <mod_id>, open_mods, clear"
	_input.text_submitted.connect(_on_submit)
	vbox.add_child(_input)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == TOGGLE_KEY:
		_toggle()
		get_viewport().set_input_as_handled()
		return
	if _open and event.is_action_pressed("ui_cancel"):
		_toggle()
		get_viewport().set_input_as_handled()

func _toggle() -> void:
	_open = not _open
	_panel.visible = _open
	if _open:
		_prev_mouse_mode = Input.mouse_mode
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		_input.grab_focus()
	else:
		Input.mouse_mode = _prev_mouse_mode
		_input.release_focus()

func _on_submit(text: String) -> void:
	text = text.strip_edges()
	_input.text = ""
	if not text.is_empty():
		_print("> " + text)
		_run(text)
	_input.grab_focus()

func _run(line: String) -> void:
	var parts := line.split(" ", false)
	var cmd := parts[0].to_lower()
	var args := parts.slice(1)
	match cmd:
		"help":
			_print("commands: help, mods, note <text>, has <mod_id>, open_mods, clear")
		"mods":
			for m in _loader.mod_list():
				_print(" - " + m)
		"note":
			if args.is_empty():
				_print("usage: note <text>")
			else:
				_loader.note(" ".join(args))
				_print("noted")
		"has":
			if args.is_empty():
				_print("usage: has <mod_id>")
			else:
				_print(str(_loader.has_mod(args[0])))
		"open_mods":
			_loader.open_mods_folder()
		"clear":
			_log.text = ""
		_:
			_print("unknown command: " + cmd)

func _print(line: String) -> void:
	_log.append_text(line + "\\n")
	_log.scroll_to_line(_log.get_line_count())
'''

def install_example(mods_dir: Path):
    target = mods_dir / "console"
    if target.exists():
        return
    target.mkdir(parents=True, exist_ok=True)
    (target / "mod.json").write_text(EXAMPLE_MOD_JSON, encoding="utf-8")
    (target / "main.gd").write_text(EXAMPLE_MOD_GD, encoding="utf-8")
    say(f"wrote example mod -> {target}")

def migrate_mods(old_dir: Path, new_dir: Path):
    """Move mods from the pre-2.x user:// location into the new folder
    beside the game, the first time the new folder doesn't exist yet."""
    if new_dir.exists() or not old_dir.is_dir():
        return
    if not any(old_dir.iterdir()):
        return
    say(f"moving existing mods {old_dir} -> {new_dir}")
    shutil.move(str(old_dir), str(new_dir))

def install_uninstaller(folder: Path):
    """Drop a double-clickable uninstaller next to the exe, so removing mods
    doesn't require opening a terminal."""
    if not getattr(sys, "frozen", False):
        return
    exe_name = Path(sys.executable).name
    bat_path = folder / "Uninstall Mods.bat"
    bat_path.write_text(
        "@echo off\r\n"
        f'"%~dp0{exe_name}" --uninstall\r\n'
        "pause\r\n",
        encoding="utf-8",
    )
    say(f"wrote uninstaller -> {bat_path.name}")

def say(msg):
    print(f"  {msg}")

def die(msg):
    print(f"\nERROR: {msg}\n", file=sys.stderr)
    if getattr(sys, "frozen", False):
        try:
            input("Press Enter to close this window . . . ")
        except (EOFError, OSError):
            pass
    sys.exit(1)

def find_steam_appid(pck_path: Path):
    """Best-effort: walk up from the pck to steamapps/appmanifest_*.acf and
    match this install's folder name, so a last-resort restore can point
    Steam at 'verify integrity of game files' instead of giving up."""
    parents = list(pck_path.resolve().parents)
    for i, p in enumerate(parents):
        if p.name.lower() != "common" or i == 0 or i + 1 >= len(parents):
            continue
        installdir = parents[i - 1].name
        steamapps = parents[i + 1]
        if steamapps.name.lower() != "steamapps":
            continue
        for acf in steamapps.glob("appmanifest_*.acf"):
            try:
                text = acf.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            if re.search(rf'"installdir"\s*"{re.escape(installdir)}"', text, re.I):
                m = re.search(r'"appid"\s*"(\d+)"', text)
                if m:
                    return m.group(1)
    return None

def request_steam_verify(appid: str) -> bool:
    if platform.system() != "Windows":
        return False
    try:
        os.startfile(f"steam://validate/{appid}")
        return True
    except OSError:
        return False

def find_beside(root: Path):
    pcks = sorted(root.glob("*.pck"))
    exes = [
        p for p in sorted(root.iterdir())
        if p.is_file()
        and (p.suffix.lower() == ".exe" or (os.access(p, os.X_OK) and not p.suffix))
        and "modloader" not in p.name.lower()
    ]
    return (pcks[0] if pcks else None), (exes[0] if exes else None)

def is_process_running(exe_name: str) -> bool:
    if platform.system() != "Windows":
        return False
    try:
        out = subprocess.check_output(
            ["tasklist", "/FI", f"IMAGENAME eq {exe_name}"],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except Exception:
        return False
    return exe_name.lower() in out.lower()

def do_uninstall(pck_path: Path, skip_confirm: bool):
    say("preparing to uninstall")

    mods_dirs = [pck_path.parent / "mods"]
    log_file = None
    try:
        p = Pck(pck_path)
        pb = p.read_file(PROJECT_BINARY)
        if pb is not None:
            settings = {}
            for k, v in ecfg_parse(pb):
                s = dec_string(v)
                if s is not None:
                    settings[k] = s
            user_root = user_data_dir(settings)
            mods_dirs.append(user_root / "mods")
            log_file = user_root / "modloader.log"
    except SystemExit:
        pass

    mods_dirs = [d for d in dict.fromkeys(mods_dirs) if d.is_dir()]
    undo = load_undo(pck_path)
    backup = pck_path.with_suffix(pck_path.suffix + ".vanilla")
    sc = sidecar_path(pck_path)

    still_patched = False
    appid = None
    if not undo and not backup.is_file():
        try:
            still_patched = Pck(pck_path).read_file(STAMP_RES) is not None
        except SystemExit:
            pass
        if still_patched:
            appid = find_steam_appid(pck_path)

    to_remove = []
    if undo:
        to_remove.append("pck patch (restores vanilla index, truncates appended data)")
    elif backup.is_file():
        to_remove.append(f"pck patch (restores from {backup.name})")
    elif still_patched:
        if appid:
            to_remove.append(
                f"pck patch — no local backup found; will ask Steam to verify/repair "
                f"app {appid} instead"
            )
        else:
            to_remove.append(
                "pck patch — no local backup found and the Steam app could not be "
                "identified automatically; you'll need to verify game files in "
                "Steam yourself afterward"
            )
    if backup.is_file():
        to_remove.append(f"backup file: {backup.name}")
    if sc.is_file() and not undo:
        to_remove.append(f"undo record: {sc.name}")
    for d in mods_dirs:
        n = sum(1 for _ in d.iterdir())
        to_remove.append(f"mods folder ({n} item(s)): {d}")
    if log_file and log_file.is_file():
        to_remove.append(f"log file: {log_file}")

    if not to_remove:
        say("nothing to uninstall — already clean")
        return

    say("this will remove:")
    for item in to_remove:
        say(" - " + item)

    if not skip_confirm:
        reply = input("\n  Proceed? [y/N] ").strip().lower()
        if reply != "y":
            say("cancelled")
            return

    pck_restored = True
    if undo:
        apply_undo(pck_path, undo)
        say("pck restored (index pointer reset, appended data truncated)")
    elif backup.is_file():
        shutil.copy2(backup, pck_path)
        say("pck restored from .vanilla backup")
    elif still_patched:
        if appid and request_steam_verify(appid):
            say(f"asked Steam to verify/repair app {appid} — it'll finish "
                "restoring the original pck once that check completes")
            pck_restored = False
        else:
            say("could not ask Steam automatically — in Steam, right-click "
                "Machine Party -> Properties -> Installed Files -> "
                "'Verify integrity of game files' to finish restoring it")
            pck_restored = False

    if backup.is_file():
        backup.unlink()
        say(f"removed {backup.name}")

    if sc.is_file():
        sc.unlink()

    for d in mods_dirs:
        shutil.rmtree(d)
        say(f"removed mods folder: {d}")

    if log_file and log_file.is_file():
        log_file.unlink()
        say("removed modloader.log")

    if pck_restored:
        say("uninstall complete — the game is back to vanilla")
    else:
        say("mods and traces removed — the pck itself still needs Steam to "
            "finish repairing it (see above)")
    say("if you set a Steam launch option pointing at this tool, remove it "
        "from the game's Properties too")

def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--pck", type=Path)
    ap.add_argument("--exe", type=Path)
    ap.add_argument("--bootstrap", type=Path)
    ap.add_argument("--play", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--uninstall", action="store_true",
                     help="restore the vanilla pck AND delete the mods folder, "
                          "backups and logs")
    ap.add_argument("--yes", action="store_true",
                     help="skip the confirmation prompt for --uninstall")
    args, rest = ap.parse_known_args()

    here = Path(getattr(sys, "_MEIPASS", Path(__file__).parent)).resolve()
    root = Path(sys.executable).parent if getattr(sys, "frozen", False) else here
    auto_pck, auto_exe = find_beside(root)

    pck_path = Path(args.pck or auto_pck or die("no .pck found — pass --pck")).resolve()
    if not pck_path.is_file():
        die(f"no such file: {pck_path}")

    print(f"\nMachine Party mod loader installer v{LOADER_VERSION}")

    if args.uninstall:
        do_uninstall(pck_path, args.yes)
        return

    if args.restore:
        undo = load_undo(pck_path)
        if undo:
            apply_undo(pck_path, undo)
            say("restored (index pointer reset, appended data truncated)")
        else:
            backup = pck_path.with_suffix(pck_path.suffix + ".vanilla")
            if backup.is_file():
                shutil.copy2(backup, pck_path)
                say("restored from .vanilla backup")
            else:
                still_patched = False
                try:
                    still_patched = Pck(pck_path).read_file(STAMP_RES) is not None
                except SystemExit:
                    pass
                if not still_patched:
                    say("already vanilla — nothing to restore")
                else:
                    appid = find_steam_appid(pck_path)
                    if appid and request_steam_verify(appid):
                        say(f"no local backup found — asked Steam to verify/repair app {appid}")
                    else:
                        say("no local backup found and the Steam app could not be identified "
                            "— in Steam, right-click Machine Party -> Properties -> "
                            "Installed Files -> 'Verify integrity of game files'")
        return

    if args.bootstrap:
        bootstrap_src = Path(args.bootstrap).read_text(encoding="utf-8")
    else:
        embedded = here / "bootstrap.gd"
        bootstrap_src = (
            embedded.read_text(encoding="utf-8") if embedded.is_file() else BOOTSTRAP
        )

    p, _ = patch(pck_path, bootstrap_src, force=args.force)

    settings = {}
    for k, v in ecfg_parse(p.read_file(PROJECT_BINARY)):
        s = dec_string(v)
        if s is not None:
            settings[k] = s

    mods_dir = pck_path.parent / "mods"
    migrate_mods(user_data_dir(settings) / "mods", mods_dir)
    mods_dir.mkdir(parents=True, exist_ok=True)
    install_example(mods_dir)
    say(f"mods folder: {mods_dir}")
    install_uninstaller(pck_path.parent)

    cmd = rest or ([str(args.exe or auto_exe)] if (args.play and (args.exe or auto_exe)) else None)
    if cmd and cmd[0] not in ("None", ""):
        say(f"launching: {cmd[0]}")
        subprocess.Popen(cmd)
    else:
        exe_for_check = args.exe or auto_exe
        if exe_for_check and is_process_running(exe_for_check.name):
            say(f"NOTE: {exe_for_check.name} is already running — close and "
                "restart it, the patch only takes effect on next launch")
        if getattr(sys, "frozen", False):
            print()
            try:
                input("Done. Press Enter to close this window . . . ")
            except (EOFError, OSError):
                pass
            return
    print()

BOOTSTRAP = r"""extends Node


const LOADER_VERSION := "2"
const API_VERSION := 1

var mods_root: String = ""
var mods: Array = []
var _by_id: Dictionary = {}
var _log: PackedStringArray = PackedStringArray()

var _kept_overrides: Array[GDScript] = []


func _init() -> void:
	mods_root = _resolve_mods_root()
	note("modloader v%s  api %d" % [LOADER_VERSION, API_VERSION])
	note("mods root: " + mods_root)
	_discover()
	_start_mods()

func _ready() -> void:
	name = "ModLoader"
	for m in mods:
		var n: Node = m.get("node")
		if n == null:
			continue
		n.name = str(m["id"])
		add_child(n)
		if n.has_method("_mod_ready"):
			n.call("_mod_ready", self)
	_flush_log()

func _resolve_mods_root() -> String:
	var exe_dir := OS.get_executable_path().get_base_dir()
	var beside := exe_dir.path_join("mods")
	if DirAccess.dir_exists_absolute(beside):
		return beside
	DirAccess.make_dir_recursive_absolute("user://mods")
	return "user://mods"


func _discover() -> void:
	var d := DirAccess.open(mods_root)
	if d == null:
		note("cannot open mods root")
		return

	var found: Array = []
	for sub in d.get_directories():
		if sub.begins_with("."):
			continue
		var dir := mods_root.path_join(sub)
		var manifest_path := dir.path_join("mod.json")
		if not FileAccess.file_exists(manifest_path):
			note("skip %s: no mod.json" % sub)
			continue

		var raw := FileAccess.get_file_as_string(manifest_path)
		var parsed = JSON.parse_string(raw)
		if typeof(parsed) != TYPE_DICTIONARY:
			note("skip %s: mod.json is not valid JSON" % sub)
			continue

		found.append({
			"id": str(parsed.get("id", sub)),
			"name": str(parsed.get("name", sub)),
			"version": str(parsed.get("version", "0.0.0")),
			"entry": str(parsed.get("entry", "main.gd")),
			"priority": int(parsed.get("priority", 0)),
			"requires": parsed.get("requires", []),
			"dir": dir,
			"node": null,
		})

	found.sort_custom(func(a, b):
		if a["priority"] == b["priority"]:
			return str(a["id"]) < str(b["id"])
		return a["priority"] < b["priority"])
	mods = found

func _start_mods() -> void:
	var present := {}
	for m in mods:
		present[str(m["id"])] = true

	var active: Array = []
	for m in mods:
		var missing: Array = []
		for req in m["requires"]:
			if not present.has(str(req)):
				missing.append(str(req))
		if not missing.is_empty():
			note("skip %s: missing dependency %s" % [m["id"], ", ".join(missing)])
			continue

		var s := compile(str(m["dir"]).path_join(str(m["entry"])))
		if s == null:
			note("skip %s: entry script failed" % m["id"])
			continue

		var n := Node.new()
		n.set_script(s)
		m["node"] = n
		active.append(m)
		_by_id[str(m["id"])] = m
		note("loaded %s v%s" % [m["id"], m["version"]])

		if n.has_method("_mod_init"):
			n.call("_mod_init", self)

	mods = active


func compile(path: String) -> GDScript:
	if not FileAccess.file_exists(path):
		note("missing script: " + path)
		return null
	var src := FileAccess.get_file_as_string(path)
	if src.is_empty():
		note("empty script: " + path)
		return null
	var s := GDScript.new()
	s.source_code = src
	if s.reload() != OK:
		note("compile failed: " + path)
		return null
	return s

func override_script(vanilla_path: String, mod_script_path: String) -> GDScript:
	if not ResourceLoader.exists(vanilla_path):
		note("no vanilla script at " + vanilla_path)
		return null
	var s := compile(mod_script_path)
	if s == null:
		return null
	s.take_over_path(vanilla_path)
	_kept_overrides.append(s)
	note("override %s <- %s" % [vanilla_path, mod_script_path])
	return s

func override_script_rel(mod_id: String, vanilla_path: String, rel: String) -> GDScript:
	var d := dir_of(mod_id)
	if d.is_empty():
		return null
	return override_script(vanilla_path, d.path_join(rel))

func dir_of(mod_id: String) -> String:
	if _by_id.has(mod_id):
		return str(_by_id[mod_id]["dir"])
	return ""

func has_mod(mod_id: String) -> bool:
	return _by_id.has(mod_id)

func mod_list() -> Array:
	var out: Array = []
	for m in mods:
		out.append("%s v%s" % [m["id"], m["version"]])
	return out

func open_mods_folder() -> void:
	OS.shell_open(ProjectSettings.globalize_path(mods_root))

func note(msg: String) -> void:
	var line := "[modloader] " + msg
	print(line)
	_log.append(line)

func _flush_log() -> void:
	var f := FileAccess.open("user://modloader.log", FileAccess.WRITE)
	if f:
		f.store_string("\n".join(_log))
		f.close()
"""

if __name__ == "__main__":
    main()
