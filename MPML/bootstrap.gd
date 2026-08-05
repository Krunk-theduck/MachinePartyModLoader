extends Node

## Machine Party external mod loader — injected bootstrap.
## Runs as the FIRST autoload, before any game script is instantiated.
## Mods are plain .gd files in a folder outside the game install.

const LOADER_VERSION := "2"
const API_VERSION := 1

var mods_root: String = ""
var mods: Array = []              # active mods, in load order
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
