extends Node

const LOADER_VERSION := "@MPML_VERSION@"
const API_VERSION := @MPML_API_VERSION@

const LOADER_DIR_NAME := "MachinePartyModLoader"

var mods_root: String = ""
var mods: Array = []
var _by_id: Dictionary = {}

var gd_root: String = ""
var plugins: Array = []
var _plugins_by_id: Dictionary = {}

var _log: PackedStringArray = PackedStringArray()

var _kept_overrides: Array[GDScript] = []

var _registries: Dictionary = {}
var _scene_hooks: Dictionary = {}
var _node_added_hooks: Array[Callable] = []
var _pending_autoloads: Dictionary = {}
var _watching: bool = false

var _used_cache: Dictionary = {"mods": {}, "plugins": {}}

var _core: ModAPI


func _init() -> void:
	_core = ModAPI.new(self, "modloader", &"loader")
	mods_root = _resolve_mods_root()
	_core.note("modloader v%s  api %d" % [LOADER_VERSION, API_VERSION])
	_core.note("mods root: " + mods_root)
	_discover()
	_start_mods()
	_start_loose()
	_prune_cache("mods")
	_prune_cache("plugins")


func _ready() -> void:
	name = "ModLoader"
	_attach_nodes(mods, "_mod_ready")
	_attach_nodes(plugins, "_plugin_ready", "_mod_ready")
	_ensure_watcher()
	_install_pending_autoloads()
	_flush_log()


func _attach_nodes(items: Array, ready_method: String, fallback_method: String = "") -> void:
	for item in items:
		var n: Node = item.get("node")
		if n == null:
			continue
		n.name = str(item["id"])
		add_child(n)
		var api = item.get("api")
		if n.has_method(ready_method):
			n.call(ready_method, api)
		elif not fallback_method.is_empty() and n.has_method(fallback_method):
			n.call(fallback_method, api)


func _resolve_mods_root() -> String:
	var exe_dir := OS.get_executable_path().get_base_dir()
	var beside := exe_dir.path_join("mods")
	if DirAccess.dir_exists_absolute(beside):
		return beside
	DirAccess.make_dir_recursive_absolute("user://mods")
	return "user://mods"


func _zip_base(z: ZIPReader) -> String:
	var best := "!"
	var best_depth := 1 << 30
	for f in z.get_files():
		var p := str(f)
		if not (p == "mod.json" or p.ends_with("/mod.json")):
			continue
		var depth := p.split("/", false).size()
		if depth < best_depth:
			best_depth = depth
			best = "" if p == "mod.json" else p.substr(0, p.length() - "mod.json".length())
	return best


func _remove_dir_recursive(path: String) -> void:
	var d := DirAccess.open(path)
	if d == null:
		return
	d.include_hidden = true
	for f in d.get_files():
		d.remove(f)
	for sub in d.get_directories():
		_remove_dir_recursive(path.path_join(sub))
	DirAccess.remove_absolute(path)


func _prune_cache(kind: String) -> void:
	var kind_dir := _core.cache_path().path_join(kind)
	var d := DirAccess.open(kind_dir)
	if d == null:
		return
	var used: Dictionary = _used_cache.get(kind, {})
	for sub in d.get_directories():
		if used.has(sub):
			continue
		_remove_dir_recursive(kind_dir.path_join(sub))
		_core.note("removed unused cache: %s/%s" % [kind, sub])


func _extract_zip(zip_path: String, kind: String) -> String:
	var zip_name := zip_path.get_file()
	var z := ZIPReader.new()
	if z.open(zip_path) != OK:
		_core.note("skip %s: cannot open the zip" % zip_name)
		return ""

	var base := _zip_base(z)
	if base == "!":
		if kind == "mods":
			_core.note("skip %s: no mod.json inside" % zip_name)
			z.close()
			return ""
		base = ""

	var files := z.get_files()
	var cache_key := zip_name.get_basename()
	var out_dir := _core.cache_path().path_join(kind).path_join(cache_key)
	var stamp := "%d:%d" % [FileAccess.get_modified_time(zip_path), files.size()]
	var stamp_path := out_dir.path_join("cache_stamp.txt")

	if FileAccess.file_exists(stamp_path) and FileAccess.get_file_as_string(stamp_path) == stamp:
		z.close()
		_used_cache[kind][cache_key] = true
		_core.note("cache up to date: %s" % zip_name)
		return out_dir

	_remove_dir_recursive(out_dir)
	DirAccess.make_dir_recursive_absolute(out_dir)

	var failed := false
	for f in files:
		var inner := str(f)
		if inner.ends_with("/"):
			continue
		var rel := inner
		if not base.is_empty():
			if not inner.begins_with(base):
				continue
			rel = inner.substr(base.length())
		rel = rel.replace("\\", "/")
		if rel.is_empty() or rel.begins_with("/") or rel.contains(":") or ".." in rel.split("/"):
			continue

		var dest := out_dir.path_join(rel)
		DirAccess.make_dir_recursive_absolute(dest.get_base_dir())
		var out := FileAccess.open(dest, FileAccess.WRITE)
		if out == null:
			_core.note("cache: cannot write " + dest)
			failed = true
			continue
		out.store_buffer(z.read_file(inner))
		out.close()
	z.close()

	if failed:
		_core.note("skip %s: extraction failed" % zip_name)
		return ""

	var sf := FileAccess.open(stamp_path, FileAccess.WRITE)
	if sf:
		sf.store_string(stamp)
		sf.close()
	_used_cache[kind][cache_key] = true
	_core.note("extracted %s -> %s" % [zip_name, out_dir])
	return out_dir


func _resolve_gd_root() -> String:
	if not gd_root.is_empty():
		return gd_root
	var beside := _core.loader_path().path_join("plugins")
	if DirAccess.dir_exists_absolute(beside):
		gd_root = beside
	else:
		DirAccess.make_dir_recursive_absolute("user://plugins")
		gd_root = "user://plugins"
	return gd_root


func _gd_files(dir: String, out: Array) -> void:
	var d := DirAccess.open(dir)
	if d == null:
		return
	for f in d.get_files():
		if not f.begins_with(".") and f.ends_with(".gd"):
			out.append(dir.path_join(f))
	for sub in d.get_directories():
		if not sub.begins_with("."):
			_gd_files(dir.path_join(sub), out)


func _start_loose() -> void:
	var root := _resolve_gd_root()
	var files: Array = []
	_gd_files(root, files)

	var rd := DirAccess.open(root)
	if rd != null:
		for f in rd.get_files():
			if f.begins_with(".") or not f.to_lower().ends_with(".zip"):
				continue
			var cache_dir := _extract_zip(root.path_join(f), "plugins")
			if not cache_dir.is_empty():
				_gd_files(cache_dir, files)
	files.sort()

	for path in files:
		var stem: String = str(path).get_file().get_basename()
		if _by_id.has(stem) or _plugins_by_id.has(stem):
			_core.note("skip loose plugin %s: name already in use" % stem)
			continue

		var api := ModAPI.new(self, stem, &"plugin")
		var s := api.compile(str(path))
		if s == null:
			_core.note("skip loose plugin: %s failed to compile" % stem)
			continue

		var plugin := {
			"id": stem,
			"name": stem,
			"version": "loose",
			"dir": str(path).get_base_dir(),
			"script": s,
			"node": null,
			"api": api,
		}

		plugins.append(plugin)
		_plugins_by_id[stem] = plugin

		if not ClassDB.is_parent_class(s.get_instance_base_type(), &"Node"):
			_core.note("registered loose plugin %s (not a Node, not started)" % stem)
			continue

		var n := Node.new()
		n.set_script(s)
		plugin["node"] = n

		if n.has_method("_plugin_init"):
			n.call("_plugin_init", api)
		elif n.has_method("_mod_init"):
			n.call("_mod_init", api)

		_core.note("loaded loose plugin %s" % stem)


func _read_mod_dir(dir: String, fallback_id: String, label: String) -> Dictionary:
	var manifest_path := dir.path_join("mod.json")
	if not FileAccess.file_exists(manifest_path):
		_core.note("skip %s: no mod.json" % label)
		return {}

	var parsed = JSON.parse_string(FileAccess.get_file_as_string(manifest_path))
	if typeof(parsed) != TYPE_DICTIONARY:
		_core.note("skip %s: mod.json is not valid JSON" % label)
		return {}

	var requires = parsed.get("requires", [])
	if typeof(requires) != TYPE_ARRAY:
		requires = []

	return {
		"id": str(parsed.get("id", fallback_id)),
		"name": str(parsed.get("name", fallback_id)),
		"version": str(parsed.get("version", "0.0.0")),
		"entry": str(parsed.get("entry", "main.gd")),
		"priority": int(parsed.get("priority", 0)),
		"requires": requires,
		"dir": dir,
		"node": null,
	}


func _zip_mod(zip_path: String) -> Dictionary:
	var zip_name := zip_path.get_file()
	var cache_dir := _extract_zip(zip_path, "mods")
	if cache_dir.is_empty():
		return {}
	var m := _read_mod_dir(cache_dir, zip_name.get_basename(), zip_name)
	if m.is_empty():
		return {}
	m["zip_path"] = zip_path
	_core.note("found zip mod %s v%s" % [m["id"], m["version"]])
	return m


func _has_id(arr: Array, id: String) -> bool:
	for m in arr:
		if str(m["id"]) == id:
			return true
	return false


func _discover() -> void:
	var d := DirAccess.open(mods_root)
	if d == null:
		_core.note("cannot open mods root")
		return

	var found: Array = []
	for sub in d.get_directories():
		if sub.begins_with("."):
			continue
		var m := _read_mod_dir(mods_root.path_join(sub), sub, sub)
		if not m.is_empty():
			found.append(m)

	for f in d.get_files():
		var fname := str(f)
		if fname.begins_with(".") or not fname.to_lower().ends_with(".zip"):
			continue
		var zm := _zip_mod(mods_root.path_join(fname))
		if zm.is_empty():
			continue
		if _has_id(found, str(zm["id"])):
			_core.note("skip %s: mod id '%s' already loaded from a folder" % [fname, zm["id"]])
			continue
		found.append(zm)

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
			_core.note("skip %s: missing dependency %s" % [m["id"], ", ".join(missing)])
			continue

		var api := ModAPI.new(self, str(m["id"]), &"mod")
		var s := api.compile(str(m["dir"]).path_join(str(m["entry"])))
		if s == null:
			_core.note("skip %s: entry script failed" % m["id"])
			continue

		var n := Node.new()
		n.set_script(s)
		m["node"] = n
		m["api"] = api
		active.append(m)
		_by_id[str(m["id"])] = m
		_core.note("loaded %s v%s" % [m["id"], m["version"]])

		if n.has_method("_mod_init"):
			n.call("_mod_init", api)

	mods = active


func _install_autoload_node(autoload_name: String, s: GDScript) -> void:
	var tree := get_tree()
	if tree == null or tree.root == null:
		return
	if tree.root.has_node(autoload_name):
		return
	var base := s.get_instance_base_type()
	if base == StringName() or not ClassDB.is_parent_class(base, &"Node"):
		_core.note("register_autoload: %s is not a Node script — setting kept, node skipped" % autoload_name)
		return
	var node: Node = s.new()
	node.name = autoload_name
	tree.root.add_child.call_deferred(node)
	_core.note("autoload node ready: /root/" + autoload_name)


func _install_pending_autoloads() -> void:
	for autoload_name in _pending_autoloads:
		_install_autoload_node(str(autoload_name), _pending_autoloads[autoload_name])


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


func _flush_log() -> void:
	var log_dir := _core.loader_path().path_join("logs")

	DirAccess.make_dir_recursive_absolute(log_dir)

	var log_path := log_dir.path_join("modloader.log")
	var f := FileAccess.open(log_path, FileAccess.WRITE)
	if f:
		f.store_string("\n".join(_log))
		f.close()
	else:
		push_error("Failed to write modloader.log: " + error_string(FileAccess.get_open_error()))

class ModAPI extends RefCounted:
	var _loader
	var _mod_id: String
	var _kind: StringName

	var modID: String:
		get:
			return _mod_id

	var kind: StringName:
		get:
			return _kind

	func _init(loader, mod_id: String, mod_kind: StringName = &"mod") -> void:
		_loader = loader
		_mod_id = mod_id
		_kind = mod_kind

	func loader_path() -> String:
		return OS.get_executable_path().get_base_dir().path_join(LOADER_DIR_NAME)

	func cache_path() -> String:
		var beside := loader_path().path_join("cache")
		if DirAccess.dir_exists_absolute(beside) or DirAccess.make_dir_recursive_absolute(beside) == OK:
			return beside
		DirAccess.make_dir_recursive_absolute("user://cache")
		return "user://cache"

	func _entry() -> Dictionary:
		var table: Dictionary = _loader._by_id if _kind == &"mod" else _loader._plugins_by_id
		return table.get(_mod_id, {})

	func dir() -> String:
		return str(_entry().get("dir", ""))

	func dir_of(mod_id: String = "") -> String:
		if mod_id.is_empty():
			return dir()
		if _loader._by_id.has(mod_id):
			return str(_loader._by_id[mod_id]["dir"])
		return ""


	func _abs(rel: String) -> String:
		var d := dir()
		return "" if d.is_empty() else d.path_join(rel)

	func note(msg: String) -> void:
		var tag := "[MPML]: " if _kind == &"loader" else "[%s]: " % _mod_id
		var line := tag + msg
		print(line)
		_loader._log.append(line)

	func compile(path: String) -> GDScript:
		if path.is_empty() or not FileAccess.file_exists(path):
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

	func override_script(vanilla_path: String, rel_script: String) -> GDScript:
		if not ResourceLoader.exists(vanilla_path):
			note("no vanilla script at " + vanilla_path)
			return null
		var s := compile(_abs(rel_script))
		if s == null:
			return null
		s.take_over_path(vanilla_path)
		_loader._kept_overrides.append(s)
		note("override %s <- %s" % [vanilla_path, rel_script])
		return s
	
	func override_script_rel(mod_id: String, vanilla_path: String, rel: String) -> GDScript:
		var d := dir_of(mod_id)
		if d.is_empty():
			return null
		return override_script(vanilla_path, d.path_join(rel))


	func register_script(rel_script: String, res_id: String) -> GDScript:
		var s := compile(_abs(rel_script))
		if s == null:
			return null
		s.take_over_path(res_id)
		_loader._kept_overrides.append(s)
		note("registered script %s -> %s" % [rel_script, res_id])
		return s

	func register_autoload(autoload_name: String, rel_script: String) -> bool:
		var s := compile(_abs(rel_script))
		if s == null:
			note("register_autoload: compile failed for " + autoload_name)
			return false
		var res_path := "res://modloader/autoload_%s.gd" % autoload_name
		s.take_over_path(res_path)
		_loader._kept_overrides.append(s)
		ProjectSettings.set_setting("autoload/" + autoload_name, "*" + res_path)
		_loader._pending_autoloads[autoload_name] = s
		_loader._install_autoload_node(autoload_name, s)
		note("registered autoload %s -> %s" % [autoload_name, res_path])
		return true

	func register_global_class(class_name_str: String, rel_script: String) -> GDScript:
		var s := compile(_abs(rel_script))
		if s == null:
			note("register_global_class: compile failed for " + class_name_str)
			return null
		var res_path := "res://modloader/class_%s.gd" % class_name_str
		s.take_over_path(res_path)
		_loader._kept_overrides.append(s)
		note("registered global class %s -> %s" % [class_name_str, res_path])
		return s

	func mount_assets(rel_zip: String = "") -> bool:
		var d := dir()
		if d.is_empty():
			note("mount_assets: no folder for " + _mod_id)
			return false
		var pack_path := ""
		if rel_zip.is_empty():
			pack_path = str(_entry().get("zip_path", ""))
			if pack_path.is_empty():
				note("mount_assets: nothing to mount (not a zip mod and no rel_zip given)")
				return false
		else:
			pack_path = d.path_join(rel_zip)
		if not FileAccess.file_exists(pack_path):
			note("mount_assets: no pack at " + pack_path)
			return false
		var ok := ProjectSettings.load_resource_pack(pack_path, false)
		if ok:
			note("mounted assets from " + pack_path)
		else:
			note("mount_assets failed: " + pack_path)
		return ok

	func on_scene_script(res_script_path: String, cb: Callable) -> void:
		if not cb.is_valid():
			note("on_scene_script: invalid callable")
			return
		if not _loader._scene_hooks.has(res_script_path):
			_loader._scene_hooks[res_script_path] = []
		var arr: Array = _loader._scene_hooks[res_script_path]
		arr.append(cb)
		_loader._ensure_watcher()

	func on_node_added(cb: Callable) -> void:
		if not cb.is_valid():
			note("on_node_added: invalid callable")
			return
		_loader._node_added_hooks.append(cb)
		_loader._ensure_watcher()

	func on_main_menu(cb: Callable) -> void:
		on_scene_script("res://scenes/main_menu/scripts/main_menu.gd", cb)

	func on_lobby(cb: Callable) -> void:
		on_scene_script("res://scenes/lobby/scripts/lobby_scene.gd", cb)

	func _autoload_node(node_name: String) -> Node:
		var tree: SceneTree = _loader.get_tree()
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

	func has_mod(mod_id: String) -> bool:
		return _loader._by_id.has(mod_id)

	func mod_list() -> Array:
		var out: Array = []
		for m in _loader.mods:
			out.append("%s v%s" % [m["id"], m["version"]])
		return out

	func has_plugin(plugin_id: String) -> bool:
		return _loader._plugins_by_id.has(plugin_id)

	func plugin_list() -> Array:
		var out: Array = []
		for plugin in _loader.plugins:
			out.append(str(plugin["id"]))
		return out

	func plugin_dir_of(plugin_id: String) -> String:
		if plugin_id.is_empty():
			return dir()
		if _loader._plugins_by_id.has(plugin_id):
			return str(_loader._plugins_by_id[plugin_id]["dir"])
		return ""

	func plugin_script(plugin_id: String) -> GDScript:
		if _loader._plugins_by_id.has(plugin_id):
			return _loader._plugins_by_id[plugin_id].get("script") as GDScript
		return null

	func registry(reg_name: String) -> Dictionary:
		if not _loader._registries.has(reg_name):
			_loader._registries[reg_name] = {}
		var r: Dictionary = _loader._registries[reg_name]
		return r

	func open_mods_folder() -> void:
		OS.shell_open(ProjectSettings.globalize_path(_loader.mods_root))

	func open_plugins_folder() -> void:
		OS.shell_open(ProjectSettings.globalize_path(_loader._resolve_gd_root()))

	func api_version() -> int:
		return API_VERSION

	func loader_version() -> String:
		return LOADER_VERSION

	func has_api(n) -> bool:
		if n is int:
			return API_VERSION >= int(n)
		return has_method(String(n))