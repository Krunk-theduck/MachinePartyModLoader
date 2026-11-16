extends Node
# these are filled out by the mod loader
const LOADER_VERSION := "@MPML_VERSION@"
const API_VERSION := @MPML_API_VERSION@

# API availability is detected from the methods exposed by this node.
# Add a public API method and has_api() will recognize it automatically.

var mods_root: String = ""
var mods: Array = []
var _by_id: Dictionary = {}

# Loose scripts are plugins, kept separate from manifest/zip mods.
var gd_root: String = ""
var plugins: Array = []
var _plugins_by_id: Dictionary = {}

var _log: PackedStringArray = PackedStringArray()

var _kept_overrides: Array[GDScript] = []

# --- API v2 additions -------------------------------------------------------
var _registries: Dictionary = {}
var _scene_hooks: Dictionary = {}
var _node_added_hooks: Array[Callable] = []
var _pending_autoloads: Dictionary = {}
var _watching: bool = false

# --- zip mods ---------------------------------------------------------------
# zip file path -> ZIPReader (kept open for the whole session so mod scripts
# can be compiled from inside the archive at any time)
var _zip_readers: Dictionary = {}


func _init() -> void:
        mods_root = _resolve_mods_root()
        note("modloader v%s  api %d" % [LOADER_VERSION, API_VERSION])
        note("mods root: " + mods_root)
        _discover()
        _start_mods()
        _start_loose()


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
                if n.has_method(ready_method):
                        n.call(ready_method, self)
                elif not fallback_method.is_empty() and n.has_method(fallback_method):
                        n.call(fallback_method, self)


func _resolve_mods_root() -> String:
        var exe_dir := OS.get_executable_path().get_base_dir()
        var beside := exe_dir.path_join("mods")
        if DirAccess.dir_exists_absolute(beside):
                return beside
        DirAccess.make_dir_recursive_absolute("user://mods")
        return "user://mods"


# --- loose scripts (plugins) ----------------------------------------------------
# Every .gd file in MachinePartyModLoader/plugins loads automatically.
# Plugins are tracked separately from mods. Scripts that extend Node are started;
# anything else is just compiled and registered.

func _resolve_gd_root() -> String:
        if not gd_root.is_empty():
                return gd_root
        var exe_dir := OS.get_executable_path().get_base_dir()
        var beside := exe_dir.path_join("MachinePartyModLoader").path_join("plugins")
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
        files.sort()

        for path in files:
                var stem: String = str(path).get_file().get_basename()
                if _by_id.has(stem) or _plugins_by_id.has(stem):
                        note("skip loose plugin %s: name already in use" % stem)
                        continue

                var s := compile(str(path))
                if s == null:
                        note("skip loose plugin: %s failed to compile" % stem)
                        continue

                var plugin := {
                        "id": stem,
                        "name": stem,
                        "version": "loose",
                        "dir": str(path).get_base_dir(),
                        "script": s,
                        "node": null,
                }

                plugins.append(plugin)
                _plugins_by_id[stem] = plugin

                if not ClassDB.is_parent_class(s.get_instance_base_type(), &"Node"):
                        note("registered loose plugin %s (not a Node, not started)" % stem)
                        continue

                var n := Node.new()
                n.set_script(s)
                plugin["node"] = n

                if n.has_method("_plugin_init"):
                        n.call("_plugin_init", self)
                elif n.has_method("_mod_init"):
                        # Backwards compatibility for loose scripts using the old callback.
                        n.call("_mod_init", self)

                note("loaded loose plugin %s" % stem)


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

        # A mod can also be a single .zip file sitting in mods/.
        for f in d.get_files():
                var fname := str(f)
                if fname.begins_with(".") or not fname.to_lower().ends_with(".zip"):
                        continue
                var zm := _zip_mod(mods_root.path_join(fname))
                if zm.is_empty():
                        continue
                if _has_id(found, str(zm["id"])):
                        note("skip %s: mod id '%s' already loaded from a folder" % [fname, zm["id"]])
                        continue
                found.append(zm)

        found.sort_custom(func(a, b):
                if a["priority"] == b["priority"]:
                        return str(a["id"]) < str(b["id"])
                return a["priority"] < b["priority"])
        mods = found


func _has_id(arr: Array, id: String) -> bool:
        for m in arr:
                if str(m["id"]) == id:
                        return true
        return false


# --- zip mods ----------------------------------------------------------------
# The zip needs a mod.json at its root ("mod.json").
# Everything else (entry script, other files) is resolved relative to that
# folder. The reader stays open so scripts inside the zip can be compiled at
# any time via compile() / register_script() / override_script().

func _zip_base(z: ZIPReader) -> String:
        # Returns "" when mod.json sits at the zip root, the wrapping folder
        # ("Folder/", with trailing slash) otherwise, or "!" when there is none.
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


func _zip_mod(zip_path: String) -> Dictionary:
        var zip_name := zip_path.get_file()
        var z := ZIPReader.new()
        if z.open(zip_path) != OK:
                note("skip %s: cannot open the zip" % zip_name)
                return {}
        var base := _zip_base(z)
        if base == "!":
                note("skip %s: no mod.json inside" % zip_name)
                return {}

        var raw: PackedByteArray = z.read_file(base + "mod.json")
        var parsed = JSON.parse_string(raw.get_string_from_utf8())
        if typeof(parsed) != TYPE_DICTIONARY:
                note("skip %s: mod.json is not valid JSON" % zip_name)
                return {}

        var entry: String = str(parsed.get("entry", "main.gd"))
        var entry_path := base + entry
        if not z.file_exists(entry_path):
                note("skip %s: entry script '%s' is not in the zip" % [zip_name, entry])
                return {}

        var fallback := zip_name.get_basename()
        _zip_readers[zip_path] = z
        note("found zip mod %s v%s" % [str(parsed.get("id", fallback)), str(parsed.get("version", "0.0.0"))])
        return {
                "id": str(parsed.get("id", fallback)),
                "name": str(parsed.get("name", fallback)),
                "version": str(parsed.get("version", "0.0.0")),
                "entry": entry_path,   # zip-internal path; dir is the zip itself
                "priority": int(parsed.get("priority", 0)),
                "requires": parsed.get("requires", []),
                "dir": zip_path,
                "zip_base": base,
                "node": null,
        }


func _zip_read(path: String) -> String:
        # Resolves "<zip>/inner/path.gd" (the shape dir_of() + path_join() produce
        # for zip mods) against the open reader, honouring the wrapping folder.
        if _zip_readers.is_empty():
                return ""
        for zip_path in _zip_readers:
                var zp := str(zip_path)
                if not (path == zp or path.begins_with(zp + "/")):
                        continue
                var z: ZIPReader = _zip_readers[zip_path]
                var inner := path.substr(zp.length())
                if inner.begins_with("/"):
                        inner = inner.substr(1)
                var base := ""
                for m in mods:
                        if str(m["dir"]) == zp and m.has("zip_base"):
                                base = str(m["zip_base"])
                var candidates: Array = [inner]
                if not base.is_empty() and not inner.begins_with(base):
                        candidates.append(base + inner)
                for c in candidates:
                        if z.file_exists(str(c)):
                                return z.read_file(str(c)).get_string_from_utf8()
                return ""
        return ""


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
        var src := _zip_read(path)
        if src.is_empty() and FileAccess.file_exists(path):
                src = FileAccess.get_file_as_string(path)
        if src.is_empty():
                note("missing or empty script: " + path)
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


# --- plugin API --------------------------------------------------------------

func has_plugin(plugin_id: String) -> bool:
        return _plugins_by_id.has(plugin_id)


func plugin_list() -> Array:
        var out: Array = []
        for plugin in plugins:
                out.append(str(plugin["id"]))
        return out


func plugin_dir_of(plugin_id: String) -> String:
        if _plugins_by_id.has(plugin_id):
                return str(_plugins_by_id[plugin_id]["dir"])
        return ""


func plugin_script(plugin_id: String) -> GDScript:
        if _plugins_by_id.has(plugin_id):
                return _plugins_by_id[plugin_id].get("script") as GDScript
        return null


func open_plugins_folder() -> void:
        OS.shell_open(ProjectSettings.globalize_path(_resolve_gd_root()))


# --- API v2: asset packs ----------------------------------------------------

func mount_assets(mod_id: String, rel_zip: String) -> bool:
        var d := dir_of(mod_id)
        if d.is_empty():
                note("mount_assets: unknown mod " + mod_id)
                return false
        # A zip mod can mount itself with an empty rel_zip; folder mods join the
        # relative path as before.
        var pack_path := d if rel_zip.is_empty() else d.path_join(rel_zip)
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

        var api_name := String(n)
        # Public methods are the API registry: no duplicate name list to maintain.
        return has_method(api_name)


func note(msg: String) -> void:
        var line := "[modloader] " + msg
        print(line)
        _log.append(line)


func _flush_log() -> void:
        var game_dir := OS.get_executable_path().get_base_dir()
        var log_dir := game_dir.path_join("MachinePartyModLoader").path_join("logs")

        DirAccess.make_dir_recursive_absolute(log_dir)

        var log_path := log_dir.path_join("modloader.log")
        var f := FileAccess.open(log_path, FileAccess.WRITE)
        if f:
                f.store_string("\n".join(_log))
                f.close()
        else:
                push_error("Failed to write modloader.log: " + error_string(FileAccess.get_open_error()))