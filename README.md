# Machine Party Mod Loader
This page has two parts:
  - Getting Started (below) - for anyone who just wants to install mods.
  - For Mod Authors & Developers (way at the bottom) - for nerds who want to either confirm the safety, or the use of AI in this project (hang off, almost the entire loader was made with AI)

If you're not planning to write a mod or complain, you can stop reading as soon
as you hit the second part.

## How to Use
* **Proxy DLL (Windows):** copy `winmm.dll` next to `Machine Party.exe`. Launch as normal.
* **Launcher (any OS):** set the Steam launch option to `"<path>/MachinePartyModLoader" %command%`.
  On Linux/Mac you can also put the launcher binary INSIDE the `MachinePartyModLoader`
  folder next to the game (the file and the folder can't share a name there) and use
  `"<path>/MachinePartyModLoader/MachinePartyModLoader" %command%`.
* Mods go in `mods/` next to the game exe - a mod is a **folder** with a
  `mod.json`, or a single **`.zip`** file containing one (see the mod author
  section). Loose `.gd` scripts go in `gd/` next to the game exe - they load
  automatically, no mod.json needed.
* After a game update, run the loader once (or just start the game through Steam) -
  it notices the changed pck and rebuilds the modded copy on its own.
* Uninstall: delete `winmm.dll` / the launch option, and run `MachinePartyModLoader --uninstall`
  (removes the modded pck + record; your `mods/` and `gd/` folders are left alone).
* The loader keeps its own stuff in `MachinePartyModLoader/` next to the game:
  `modloader.log` (in-game loader log), `mpml.log` / `mpml_proxy.log` (launcher/proxy
  logs sit next to the exe), `loader.cfg` (the settings file, see below) and
  `cache/` (compile reports + verdict cache - safe to delete).

## Settings (`MachinePartyModLoader/loader.cfg`)
A plain `key = value` file, created with documented defaults on first run.
Lines starting with `#` are comments; unknown keys are ignored; deleting the
file brings every default back.

    console = false    Windows: give the game a debug console window so the
                       loader logs are visible live. Off by default - set
                       `console = true` to turn it on.

If you previously used the old Python installer, restore a vanilla pck first
(`--uninstall` in the old tool, or Steam "Verify integrity of game files").

## Script checking
Every launch compile-checks the `.gd` scripts in `mods/` and `gd/` and logs syntax
errors (with file + line) before the game starts - launches are never blocked.
Scripts packed inside `.zip` mods are checked too (reported as `mod.zip!path.gd`).

    MachinePartyModLoader --compile            check gd/ + mods/, exit 1 on errors
    MachinePartyModLoader --compile <path>     check one file, folder or .zip

The full report lands in `MachinePartyModLoader/cache/compile_report.txt`; verdicts
are cached in `MachinePartyModLoader/cache/compile/` so only changed scripts are
re-parsed. It is a syntax check only - type errors stay the engine's job.

## Test (Linux/Mac)
    python3 tests/compare.py build/main/MachinePartyModLoader /path/to/modloader_patch.py


# For Mod Authors & Developers

## Mods
A mod is a folder in `mods/` with a `mod.json` and an entry script (`main.gd`):

    {
      "id": "console",
      "name": "Example console mod",
      "version": "1.0.0",
      "entry": "main.gd",
      "priority": 0,
      "requires": []
    }

### Zip mods
The same thing can ship as a single `.zip` file in `mods/` - no extraction:

    mods/my_mod.zip        mod.json + main.gd at the zip root, or
    mods/downloaded.zip    ...inside one wrapping folder ("MyMod-main/"),
                           like a "Download ZIP" from a git host

The loader opens the zip (Godot's `ZIPReader`), reads `mod.json`, compiles the
entry script straight from the archive and starts it like any other mod -
priority, requires, `dir_of()`, `register_script()` etc. all work; `dir_of()`
returns the zip's path and paths joined onto it resolve inside the archive.
To get at assets, mount the archive into `res://` with
`ModLoader.mount_assets(my_id, "")` (a zip mod can mount itself). If a folder
and a zip carry the same mod id, the folder wins and the zip is skipped.

Mods can call the loader (injected as the `ModLoader` autoload, passed to
`_mod_init` / `_mod_ready`) for API level 2: `compile`, `override_script`,
`override_script_rel`, `dir_of`, `has_mod`, `mod_list`, `open_mods_folder`,
`mount_assets`, `register_autoload`, `register_global_class`, `register_script`,
`on_scene_script`, `on_node_added`, `on_main_menu`, `on_lobby`, `is_local_couch`,
`game_mode`, `registry`, `api_version`, `loader_version`, `has_api`.

Loose scripts in `gd/` behave like one-file mods: `.gd` files that extend `Node`
are started automatically (with `_mod_init` support); anything else is compiled
and registered. The mod id of a loose script is its file name.

## How the pieces fit
    asp2      THE file API (asp::fs, github.com/dankmeme01/asp2). Every disk
              operation in the loader - launcher, proxy DLL, patcher, logger -
              goes through it, never through raw std::filesystem/stdio. CMake
              fetches it (pinned commit) together with miniz (zip reading for
              the compile check) and asp2's own deps (fmt, geode::result).
    patch/    mpml_patch: the pck reader/patcher (pck.cpp), the modded-pck logic
              (patch.cpp), the .gd compile check (gdcheck.cpp) and the embedded
              bootstrap.gd + example mod. Used by BOTH the launcher and the proxy.
    main/     the launcher binary (Steam launch option / standalone).
    proxy/    winmm.dll: loads before Godot, patches the pck if needed, then
              hooks CreateFileW so "<game>.pck" opens "<game>.modded.pck".
    Global/   shared code: mpml_util (exe path, hashing, sorted listings),
              mpml_settings (loader.cfg) and mpml_logger.

## One version to rule them all
The loader version lives in exactly ONE place: `mpml::kVersion` in `patch/version.h`.
Everything else derives from it automatically:
* `patch/bootstrap.gd` contains the placeholder `@MPML_VERSION@`; when the modded
  pck is built, the patcher injects `kVersion`. (A version bump therefore also
  changes the bootstrap bytes, which rebuilds the modded pck - automatically.)
* CMake regenerates the Windows version resources (`version_loader.rc` /
  `version_proxy.rc`) from `kVersion` on every configure - the exe/dll properties
  always match the loader version.
Never edit `LOADER_VERSION` in bootstrap.gd by hand; bump `kVersion` instead.

## Building (Windows)
    cmake -B build
    cmake --build build --config Release
Outputs: `build/main/MachinePartyModLoader.exe` and `build/proxy/winmm.dll`.
NASM is bundled in `Scripts/nasm/` (the winmm jump table); MinHook, asp2 and
miniz are fetched by CMake on first configure (network needed once).
asp2's headers use C++23 (deducing `this`) - use a current VS 2022 (17.13+).
The first configure needs internet access to fetch asp2 (pinned commit) + its
dependencies via CPM.

## Building (Linux/Mac)
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
(the proxy DLL is Windows-only; the launcher + patcher build everywhere -
GCC 13+/Clang 17+ for the C++23 headers)
