extends Node

## Machine Party external mod loader — injected bootstrap.
## Runs as the FIRST autoload, before any game script is instantiated.
## Mods are plain .gd files in a folder outside the game install.

const LOADER_VERSION := "3"
const API_VERSION := 2

const API_NAMES := [
	"note", "compile", "override_script", "override_script_rel", "dir_of",
	"has_mod", "mod_list", "open_mods_folder",
	"mount_assets", "register_autoload", "register_global_class", "register_script",
	"on_scene_script", "on_node_added", "on_main_menu", "on_lobby",
	"is_local_couch", "game_mode", "registry", "api_version", "loader_version", "has_api",
]

var mods_root: String = ""
var mods: Array = []              # active mods, in load order
var _by_id: Dictionary = {}
var _log: PackedStringArray = PackedStringArray()

var _kept_overrides: Array[GDScript] = []

# --- API v2 additions -------------------------------------------------------
var _registries: Dictionary = {}
var _scene_hooks: Dictionary = {}
var _node_added_hooks: Array[Callable] = []
var _pending_autoloads: Dictionary = {}
var _watching: bool = false



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
	_ensure_watcher()
	_install_pending_autoloads()
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


# --- API v2: asset packs ----------------------------------------------------

func mount_assets(mod_id: String, rel_zip: String) -> bool:
	var d := dir_of(mod_id)
	if d.is_empty():
		note("mount_assets: unknown mod " + mod_id)
		return false
	var pack_path := d.path_join(rel_zip)
	if not FileAccess.file_exists(pack_path):
		note("mount_assets: no pack at " + pack_path)
		return false
	var ok := ProjectSettings.load_resource_pack(pack_path, false)
	if ok:
		note("mounted assets for %s from %s" % [mod_id, rel_zip])
	else:
		note("mount_assets failed: " + pack_path)
	return ok


# --- API v2: script / singleton registration --------------------------------

func register_script(mod_id: String, rel_script: String, res_id: String) -> GDScript:
	var d := dir_of(mod_id)
	if d.is_empty():
		note("register_script: unknown mod " + mod_id)
		return null
	var s := compile(d.path_join(rel_script))
	if s == null:
		return null
	s.take_over_path(res_id)
	_kept_overrides.append(s)
	note("registered script %s -> %s" % [mod_id, res_id])
	return s


func register_autoload(autoload_name: String, mod_id: String, rel_script: String) -> bool:
	var d := dir_of(mod_id)
	if d.is_empty():
		note("register_autoload: unknown mod " + mod_id)
		return false
	var s := compile(d.path_join(rel_script))
	if s == null:
		note("register_autoload: compile failed for " + autoload_name)
		return false
	var res_path := "res://modloader/autoload_%s.gd" % autoload_name
	s.take_over_path(res_path)
	_kept_overrides.append(s)
	ProjectSettings.set_setting("autoload/" + autoload_name, "*" + res_path)
	_pending_autoloads[autoload_name] = s
	_install_autoload_node(autoload_name, s)
	note("registered autoload %s -> %s" % [autoload_name, res_path])
	return true


func register_global_class(class_name_str: String, mod_id: String, rel_script: String) -> GDScript:
	var d := dir_of(mod_id)
	if d.is_empty():
		note("register_global_class: unknown mod " + mod_id)
		return null
	var s := compile(d.path_join(rel_script))
	if s == null:
		note("register_global_class: compile failed for " + class_name_str)
		return null
	var res_path := "res://modloader/class_%s.gd" % class_name_str
	s.take_over_path(res_path)
	_kept_overrides.append(s)
	note("registered global class %s -> %s" % [class_name_str, res_path])
	return s


func _install_autoload_node(autoload_name: String, s: GDScript) -> void:
	var tree := get_tree()
	if tree == null or tree.root == null:
		return
	if tree.root.has_node(autoload_name):
		return
	var base := s.get_instance_base_type()
	if base == StringName() or not ClassDB.is_parent_class(base, &"Node"):
		note("register_autoload: %s is not a Node script — setting kept, node skipped" % autoload_name)
		return
	var node: Node = s.new()
	node.name = autoload_name
	tree.root.add_child.call_deferred(node)
	note("autoload node ready: /root/" + autoload_name)


func _install_pending_autoloads() -> void:
	for autoload_name in _pending_autoloads:
		_install_autoload_node(str(autoload_name), _pending_autoloads[autoload_name])


# --- API v2: lifecycle hooks ------------------------------------------------

func on_scene_script(res_script_path: String, cb: Callable) -> void:
	if not cb.is_valid():
		note("on_scene_script: invalid callable")
		return
	if not _scene_hooks.has(res_script_path):
		_scene_hooks[res_script_path] = []
	var arr: Array = _scene_hooks[res_script_path]
	arr.append(cb)
	_ensure_watcher()


func on_node_added(cb: Callable) -> void:
	if not cb.is_valid():
		note("on_node_added: invalid callable")
		return
	_node_added_hooks.append(cb)
	_ensure_watcher()


func on_main_menu(cb: Callable) -> void:
	on_scene_script("res://scenes/main_menu/scripts/main_menu.gd", cb)


func on_lobby(cb: Callable) -> void:
	on_scene_script("res://scenes/lobby/scripts/lobby_scene.gd", cb)


func _ensure_watcher() -> void:
	if _watching:
		return
	var tree := get_tree()
	if tree == null:
		return
	tree.node_added.connect(_on_node_added_internal)
	_watching = true


func _on_node_added_internal(node: Node) -> void:
	for cb in _node_added_hooks:
		if cb.is_valid():
			cb.call(node)
	if _scene_hooks.is_empty():
		return
	var scr = node.get_script()
	if scr == null or not (scr is Resource):
		return
	var p: String = (scr as Resource).resource_path
	if p.is_empty() or not _scene_hooks.has(p):
		return
	var arr: Array = _scene_hooks[p]
	for cb in arr:
		if cb is Callable and cb.is_valid():
			cb.call(node)


# --- API v2: game-mode awareness --------------------------------------------

func _autoload_node(node_name: String) -> Node:
	var tree := get_tree()
	if tree == null or tree.root == null:
		return null
	return tree.root.get_node_or_null(node_name)


func is_local_couch() -> bool:
	var gm := _autoload_node("GameManager")
	if gm == null:
		return false
	return gm.get("local_game") == true


func game_mode() -> StringName:
	var gm := _autoload_node("GameManager")
	if gm != null and gm.get("local_game") == true:
		return &"local"
	var nm := _autoload_node("NetworkManager")
	if nm != null and nm.get("active_backend") != null:
		return &"networked"
	return &"menu"


# --- API v2: cross-mod coordination + versioning ----------------------------

func registry(reg_name: String) -> Dictionary:
	if not _registries.has(reg_name):
		_registries[reg_name] = {}
	var r: Dictionary = _registries[reg_name]
	return r


func api_version() -> int:
	return API_VERSION


func loader_version() -> String:
	return LOADER_VERSION


func has_api(n) -> bool:
	if n is int:
		return API_VERSION >= int(n)
	return API_NAMES.has(String(n))


func note(msg: String) -> void:
	var line := "[modloader] " + msg
	print(line)
	_log.append(line)


func _flush_log() -> void:
	var f := FileAccess.open("user://modloader.log", FileAccess.WRITE)
	if f:
		f.store_string("\n".join(_log))
		f.close()
