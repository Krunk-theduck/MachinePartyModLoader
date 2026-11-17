# Machine Party Mod Loader

> Game Updates: When Steam updates Machine Party, it overwrites the game files that the mods are patched into. With the new C++ rewrite, you do not need to panic or download anything again. If you use the Proxy DLL or the Steam Launch Option, it handles updates automatically on launch. Otherwise, just run the loader once after an update and your mods will be active again.

This page is split into two parts:

* Getting Started for anyone who just wants to install and play with mods.
* For Mod Authors and Developers for anyone who wants to make mods, build from source, or check the AI usage in this project (fair warning, a huge chunk of this was built with AI).

If you are not planning to write a mod or complain, you can stop reading as soon as you hit the second part.

## Before we start

This mod loader was made by a single developer. It was originally built using Claude and a tiny amount of Python knowledge, and has since been completely rewritten in C++ using z.ai. Please be patient if anything is rough around the edges.

*(If you previously used the old Python version of this loader, restore a clean vanilla pack file first using the old uninstall command or use Steam validation before setting this up.)*

---

## Getting Started

### Step 1: Download the loader

Grab the latest release of the mod loader. If it arrives as a compressed archive, extract all contents first so you have the standalone executable ready.

### Step 2: Find your Machine Party folder

1. Open Steam and find Machine Party in your Library.
2. Right click the game, select Manage, and click Browse local files.
3. Open the Windows version folder inside.
4. You should see the main game executable and pack file. Keep this folder open.

### Step 3: Install the loader

You have two ways to run it:

*Option A: Proxy DLL (Windows only)*

1. Copy the proxy DLL directly into the folder next to the main game executable.
2. Launch the game normally from Steam or your desktop.

*Option B: Launcher and Steam Launch Options*

1. Put the mod loader executable in your game folder. Note that on Linux or Mac, the binary must go inside a dedicated subfolder because the file and folder cannot share the exact same name.
2. In Steam, right click Machine Party, open Properties, go to General, and find Launch Options.
3. Paste the path to the loader followed by the command parameter:
* Windows: `"C:\path\to\Machine Party_Windows\MachinePartyModLoader.exe" %command%`
* Linux or Mac: `"/path/to/Machine Party/MachinePartyModLoader/MachinePartyModLoader" %command%`



### Step 4: Installing mods

Once you run the game or loader once, a mods directory will appear next to the game executable. To install a mod, drop its folder or zip file straight into that directory. A working example folder is already included for reference.

### How do you know it worked?

Once you are in the game, press the grave key located above Tab and to the left of the number one. A debug console should pop up on screen. Press it again or hit escape to close it.

### Settings (`MachinePartyModLoader/configs/loader.cfg`)

The loader stores its logs and config inside a dedicated subfolder next to the game. The configuration file uses simple key value pairs generated on your first run. Lines starting with a hash symbol are comments:

```ini
# Windows only: opens a live debug console window alongside the game
console = true

```

### Uninstalling

1. Delete the proxy DLL from your game folder or clear your Steam launch options.
2. Run the mod loader executable from your command line with the uninstall flag to restore the original game files and remove loader records while leaving your mods intact.

---

---

# For Mod Authors and Developers

Everything below assumes you are comfortable with a terminal, C++, and GDScript. If you just wanted to play with mods, you are already done.

## How it works

1. When launched through the proxy or command line, the C++ patcher checks the game pack file.
2. If the pack file is not patched or was overwritten by a Steam update, it injects the bootstrap logic and adjusts entry points so the mod loader initializes before game scripts run.
3. Because Godot uses an append only pack format, patching and restoring takes a fraction of a second without duplicating massive files.
4. On startup, the loader scans the mods directory and plugins directory, verifies script syntax, and loads everything in priority order.

## Script checking and CLI flags

The C++ executable processes command line arguments directly to control behavior. Supported flags include:

* `--uninstall`: Restores original files and removes temporary records.
* `--play`: Applies patches if needed and immediately launches the game.
* `--compile`: Checks script syntax across all plugins and mods, exiting with code one if errors are found.
* `--compile <path>`: Checks syntax for a single specified file, folder, or zip archive.

You can also check scripts manually from a terminal. Results are saved to a report file and cached so unchanged scripts are skipped. This performs a syntax check only, leaving type validation to the game engine.

---

## Writing a Mod

### Folder Mods

Create a folder inside the mods directory containing a configuration file named `mod.json` and an entry script named `main.gd`:

```json
{
  "id": "bigger_jump",
  "name": "Bigger Jump",
  "version": "1.0.0",
  "entry": "main.gd",
  "priority": 0,
  "requires": []
}

```

```gdscript
extends Node

func _mod_init(loader) -> void:
	loader.note("bigger jump loading")

func _mod_ready(loader) -> void:
	loader.note("bigger jump ready")

```

### Zip Mods and Loose Plugins

* **Zip Mods:** You can distribute your mod as a single zip file placed directly in the mods directory without extracting it. The configuration and entry script can sit at the root or inside a single wrapper folder. Call `loader.mount_assets` to load its resources. If an extracted folder and a zip share the same mod identifier, the folder takes priority.
* **Loose Plugins:** Single script files placed in the plugins directory act as lightweight mods. Scripts extending Node start automatically, and their identifier defaults to their filename.

### Overriding Vanilla Scripts

To modify an existing game script, create a separate file that extends the original script path at the very top:

```gdscript
extends "res://scripts/player.gd"

func _ready() -> void:
	super._ready()
	print("player script has been modded")

```

Register the override inside your initialization function before the game builds scenes:

```gdscript
func _mod_init(loader) -> void:
	loader.override_script_rel("bigger_jump", "res://scripts/player.gd", "override.gd")

```

---

## Loader API

The loader is injected as an autoload singleton and passed directly into mod initialization and ready functions.

## Loader API (what `loader` gives you)
Mod entry points: `_mod_init(loader)` (before the game exists - do overrides
here) and `_mod_ready(loader)` (scene tree alive - safe to touch nodes).

Plugin entry point: `_plugin_init(loader)`
and `_plugin_ready(loader)`

|Call|Does|
|-|-|
|`loader.note(text)`|print + write to `MachinePartyModLoader/logs/modloader.log`|
|`loader.compile(abs_path)`|turn a loose `.gd` file into a usable script|
|`loader.override_script(res_path, abs_path)`|swap a vanilla script|
|`loader.override_script_rel(id, res_path, rel)`|same, relative to your mod folder|
|`loader.dir_of(id)`|that mod's folder|
|`loader.has_mod(id)`|is another mod present|
|`loader.mod_list()`|every mod loaded|
|`loader.open_mods_folder()`|pop the folder open in the file manager|

**Loader v3** (API level 2 - all additive, so older mods keep working unchanged). Feature-detect with `loader.has_api("mount_assets")` or `loader.api_version() >= 2` before calling:

|Call|Does|
|-|-|
|`loader.mount_assets(id, rel)`|mount a `.zip`/`.pck` resource pack shipped inside your mod, so its `res://` assets load|
|`loader.register_autoload(name, id, rel)`|register one of your scripts as an autoload singleton|
|`loader.register_global_class(cls, id, rel)`|register a `class_name` global so other scripts can reference it|
|`loader.register_script(id, rel, res_id)`|expose a script at a chosen `res://` path|
|`loader.on_scene_script(cb)` / `on_node_added(cb)` / `on_main_menu(cb)` / `on_lobby(cb)`|shared lifecycle hooks — one callback list every mod shares, instead of each mod racing its own `SceneTree` watcher|
|`loader.is_local_couch()` / `loader.game_mode()`|is the game in v2's local shared-screen couch mode; `"local"` / `"networked"` / `""`|
|`loader.registry(name)`|a shared, named `Dictionary` for cross-mod coordination|
|`loader.api_version()` / `loader.loader_version()` / `loader.has_api(n)`|feature/version numbers and capability detection|


**New in loader v4** (API level 3 - mostly additive however logging has changed however, older mods should keep working unchanged). Feature-detect with `loader.has_api("has_plugin")` or `loader.api_version() >= 3` before calling:
|Call|Does|
|-|-|
|`loader.modID`| is a string with the mod id|
|`loader.has_plugin(name)`|is this plugin present|
|`loader.plugin_list()`|every plugin loaded|
|`loader.open_plugins_folder()`|pop the folder open in the file manager|

---

## Building from Source

### Versioning

The loader version is defined in a single text file. During compilation, placeholders in the bootstrap script are automatically updated, and CMake regenerates version resources so executable properties always match the loader version.

### Windows Compilation

Requires Visual Studio with a recent toolchain supporting C++23 features. Internet access is required on the first CMake configure step to fetch dependencies via CPM.

```bash
cmake -B build
cmake --build build --config Release

```

### Linux and Mac Compilation

Requires a modern compiler supporting C++23 standards. The proxy DLL is Windows specific, but the launcher and patcher build natively across platforms.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

```