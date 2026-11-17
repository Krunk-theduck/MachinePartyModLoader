extends Node
# this plugin registers the "test" command into the console
const CONSOLE_MOD_ID := "console"

var loader
var passed: int = 0
var failed: int = 0
var skipped: int = 0
var _results: Array = []
var _hooks_registered := false


func _plugin_init(p_loader) -> void:
    loader = p_loader

    if not loader.has_mod(CONSOLE_MOD_ID):
        loader.note("[API TEST] console mod '%s' not loaded, 'test' command not registered" % CONSOLE_MOD_ID)
        return

    var cmds: Dictionary = loader.registry(CONSOLE_MOD_ID)
    cmds["test"] = {
        "run": _cmd_test,
        "help": "test <verbose> - run the loader API self-tests",
    }

    loader.note("registered console command: test using example.gd plugin")


func _cmd_test(args: Array) -> String:
    var verbose := args.size() > 0 and str(args[0]).to_lower() in ["verbose", "-v", "v", "all"]
    run_all()
    return build_output(verbose)


func run_all() -> void:
    passed = 0
    failed = 0
    skipped = 0
    _results.clear()

    loader.note("========== API TESTS ==========")

    test_methods()
    test_note()
    test_versions()
    test_mods()
    test_plugins()
    test_registry()
    test_game_mode()
    test_lifecycle()
    test_compile()
    test_destructive_methods()

    summary()


func build_output(verbose: bool) -> String:
    var out := PackedStringArray()
    var color := "green" if failed == 0 else "red"
    var total := passed + failed + skipped
    out.append("[color=" + color + "]API tests: "
            + str(passed) + " passed, "
            + str(failed) + " failed, "
            + str(skipped) + " skipped ("
            + str(total) + " total)[/color]"
    )

    for r in _results:
        var status: String = r["status"]
        if not verbose and status != "FAIL":
            continue

        var tint := "green"
        if status == "FAIL":
            tint = "red"
        elif status == "SKIP":
            tint = "yellow"

        var line := "[color=%s]%s[/color] %s" % [tint, status, r["name"]]
        if not str(r["reason"]).is_empty():
            line += " - " + str(r["reason"])
        out.append(line)

    if not verbose and skipped > 0:
        out.append("(use 'test verbose' to see every result)")

    return "\n".join(out)


func report(message: String) -> void:
    loader.note("[API TEST] " + message)


func _record(status: String, test_name: String, reason: String) -> void:
    _results.append({"status": status, "name": test_name, "reason": reason})
    report("%s: %s %s" % [status, test_name, reason])


func pass_test(test_name: String) -> void:
    passed += 1
    _record("PASS", test_name, "")


func fail_test(test_name: String, reason: String = "") -> void:
    failed += 1
    _record("FAIL", test_name, reason)


func skip_test(test_name: String, reason: String = "") -> void:
    skipped += 1
    _record("SKIP", test_name, reason)


func test_methods() -> void:
    var methods: Array[String] = [
        "note",
        "compile",
        "override_script",
        "override_script_rel",
        "dir_of",
        "has_mod",
        "mod_list",
        "open_mods_folder",
        "has_plugin",
        "plugin_list",
        "plugin_dir_of",
        "plugin_script",
        "open_plugins_folder",
        "mount_assets",
        "register_script",
        "register_autoload",
        "register_global_class",
        "on_scene_script",
        "on_node_added",
        "on_main_menu",
        "on_lobby",
        "is_local_couch",
        "game_mode",
        "registry",
        "api_version",
        "loader_version",
        "has_api"
    ]

    for method_name in methods:
        if loader.has_method(method_name):
            pass_test("Method exists: " + method_name)
        else:
            fail_test("Method exists: " + method_name)


func test_note() -> void:
    loader.note("Testing note()")
    pass_test("note()")


func test_versions() -> void:
    var version = loader.api_version()
    var loader_version = loader.loader_version()

    if typeof(version) == TYPE_INT:
        pass_test("api_version()")
    else:
        fail_test("api_version()", "Expected integer")

    if str(loader_version) != "":
        pass_test("loader_version()")
    else:
        fail_test("loader_version()", "Empty version")

    if typeof(loader.has_api(1)) == TYPE_BOOL:
        pass_test("has_api(1)")
    else:
        fail_test("has_api(1)", "Expected bool")

    if typeof(loader.has_api("mount_assets")) == TYPE_BOOL:
        pass_test("has_api(mount_assets)")
    else:
        fail_test("has_api(mount_assets)", "Expected bool")


func test_mods() -> void:
    var list = loader.mod_list()

    if list is Array:
        pass_test("mod_list()")
    else:
        fail_test("mod_list()", "Expected Array")

    if typeof(loader.has_mod("__missing_test_mod__")) == TYPE_BOOL:
        pass_test("has_mod()")
    else:
        fail_test("has_mod()", "Expected bool")

    if loader.dir_of("__missing_test_mod__") == "":
        pass_test("dir_of() missing mod")
    else:
        fail_test("dir_of()", "Expected empty path for missing mod")


func test_plugins() -> void:
    var list = loader.plugin_list()

    if list is Array:
        pass_test("plugin_list()")
    else:
        fail_test("plugin_list()", "Expected Array")

    if typeof(loader.has_plugin("__missing_test_plugin__")) == TYPE_BOOL:
        pass_test("has_plugin()")
    else:
        fail_test("has_plugin()", "Expected bool")

    if loader.plugin_dir_of("__missing_test_plugin__") == "":
        pass_test("plugin_dir_of() missing plugin")
    else:
        fail_test("plugin_dir_of()", "Expected empty path")

    if loader.plugin_script("__missing_test_plugin__") == null:
        pass_test("plugin_script() missing plugin")
    else:
        fail_test("plugin_script()", "Expected null")


func test_registry() -> void:
    var registry_name := "__api_test_registry__"

    var first = loader.registry(registry_name)

    if not (first is Dictionary):
        fail_test("registry()", "Expected Dictionary")
        return

    first["test"] = "working"

    var second = loader.registry(registry_name)

    if second.get("test", "") == "working":
        pass_test("registry() shared Dictionary")
    else:
        fail_test("registry()", "Dictionary was not shared")

    second.erase("test")


func test_game_mode() -> void:
    var local = loader.is_local_couch()

    if typeof(local) == TYPE_BOOL:
        pass_test("is_local_couch()")
    else:
        fail_test("is_local_couch()", "Expected bool")

    var mode = str(loader.game_mode())

    if mode in ["local", "networked", "menu"]:
        pass_test("game_mode()")
    else:
        fail_test("game_mode()", "Unexpected value: " + mode)


func test_lifecycle() -> void:
    if _hooks_registered:
        skip_test("lifecycle hooks", "Already registered by an earlier run (hooks can't be removed)")
        return
    _hooks_registered = true

    loader.on_node_added(Callable(self, "_test_node_added"))
    pass_test("on_node_added()")

    loader.on_main_menu(Callable(self, "_test_scene"))
    pass_test("on_main_menu()")

    loader.on_lobby(Callable(self, "_test_scene"))
    pass_test("on_lobby()")

    loader.on_scene_script(
        "res://scenes/main_menu/scripts/main_menu.gd",
        Callable(self, "_test_scene")
    )
    pass_test("on_scene_script()")


func _test_node_added(_node: Node) -> void:
    pass


func _test_scene(_node: Node) -> void:
    pass


func test_compile() -> void:
    var path := "user://__api_test_compile.gd"

    var f := FileAccess.open(path, FileAccess.WRITE)
    if f == null:
        skip_test("compile()", "Could not write a temp script to user://")
        return
    f.store_string("extends Node\n")
    f.close()

    var result = loader.compile(path)
    DirAccess.remove_absolute(path)

    if result is GDScript:
        pass_test("compile()")
    else:
        fail_test("compile()", "Could not compile temp script")


func test_destructive_methods() -> void:
    var methods: Array[String] = [
        "override_script",
        "override_script_rel",
        "open_mods_folder",
        "open_plugins_folder",
        "mount_assets",
        "register_script",
        "register_autoload",
        "register_global_class"
    ]

    for method_name in methods:
        if loader.has_method(method_name):
            skip_test(method_name, "Available, but not executed to avoid changing game state")
        else:
            skip_test(method_name, "Not available")


func summary() -> void:
    report("================================")
    report("TEST RESULTS")
    report("PASS: " + str(passed))
    report("FAIL: " + str(failed))
    report("SKIP: " + str(skipped))
    report("TOTAL: " + str(passed + failed + skipped))
    report("================================")