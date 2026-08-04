# Machine Party Mod Loader

This lets you add mods to Machine Party — small extras like a debug console
and an in-game chat box. You don't need to know how to code, and you don't
need to install Godot or any other program. You just need one file.

This page has two parts:

- **Getting Started** (below) — for anyone who just wants to install mods.
- **For Mod Authors & Developers** (way at the bottom) — for anyone who wants
  to build the tool from source or write their own mod.

If you're not planning to write a mod yourself, you can stop reading as soon
as you hit the second part.

\---

## Getting Started

### Step 1 — Get the file

You should have a file called `MachinePartyModLoader.exe`. If what you have
is a `.zip` file instead, right-click it and choose **Extract All** first —
you want the `.exe` file that comes out of it.

### Step 2 — Find your Machine Party folder

1. Open Steam.
2. In your **Library**, find Machine Party and right-click it.
3. Click **Manage**, then click **Browse local files**.
4. A File Explorer window will pop up.
5. Inside it, look for another folder — its name will have "Windows" in it,
   something like `Machine Party_Windows`. Open that folder too.
6. You should now see files named `Machine Party.exe` and
   `Machine Party.pck`. **This is the folder you need** — keep this window
   open, you'll use it for every step below.

If you don't see a `Machine Party_Windows` folder and instead see
`Machine Party.exe` directly, that's fine too — you're already in the right
place.

### Step 3 — Put the loader in that folder

Drag `MachinePartyModLoader.exe` into the File Explorer window from Step 2,
so it sits next to `Machine Party.exe` and `Machine Party.pck`.

### Step 4 — Run it

Double-click `MachinePartyModLoader.exe`.

Windows will very likely show a blue box titled **"Windows protected your
PC"**. This is completely normal — it happens for any program that wasn't
bought from the Microsoft Store, not because anything is actually wrong.
To get past it:

1. Click the small text that says **More info**.
2. A button labeled **Run anyway** will appear. Click it.

Some antivirus programs may also flag it for the same reason (it's a small,
unsigned tool, which looks suspicious to automated scanners even when it's
harmless). If yours does, you'll need to allow it — check your antivirus's
quarantine or notifications for an "allow" / "restore" option.

### Step 5 — Let it finish

A black window will open with some text scrolling by — that's normal, it's
setting things up. When it's done, it will say:

```
Done. Press Enter to close this window . . .
```

Press Enter, or just close the window.

### Step 6 — Install a mod (optional)

Look in the game's folder again (the one from Step 2). There's now a new
folder in there called `mods`. To install a mod someone gives you, put its
folder inside `mods`.

There's already one folder in there called `console` — that came bundled
with the loader itself, as a working example. You don't need to do anything
with it; it's just there so you can peek inside and see what a mod looks
like.

### Step 7 — Play

Start Machine Party from Steam exactly like you always have. Nothing about
that changes.

### How do you know it worked?

Once you're in the game, press the `` ` `` key — that's the small squiggly
key, usually just above **Tab** and to the left of the **1** key. A debug
console should pop up on screen. If it does, the loader is working.

### Uninstalling

Go back to the game's folder (Step 2). Double-click **`Uninstall Mods.bat`**
— it appeared there the first time you ran the loader. It will list what
it's about to remove and ask you to type `y` and press Enter to confirm.
That puts the game back to how it was before, and removes the `mods` folder.

No terminal, no typing commands — just double-click it, same as the loader
itself.

### Optional: make it run automatically

If you don't want to double-click the loader every time before you play:

1. In Steam, right-click Machine Party → **Properties**.
2. On the **General** tab, find the box labeled **Launch Options**.
3. Paste this in (fix the path if you put the loader somewhere other than
   the folder from Step 2):

```
"C:\path\to\Machine Party_Windows\MachinePartyModLoader.exe" %command%
```

Now every time you hit Play in Steam, it checks/updates the mods for you
first, then starts the game automatically — you'll never need to
double-click it again, even after a game update.

\---

## Frequently Asked Questions

**It says "no .pck found".** You put `MachinePartyModLoader.exe` in the
wrong folder — go back to Step 2 and make sure you're in the same folder as
`Machine Party.exe` and `Machine Party.pck`.

**I don't see a "Run anyway" button.** Make sure you clicked **More info**
first — the button only appears after that.

**My antivirus deleted it / won't let me run it.** This is a false positive
— unsigned tools like this one commonly get flagged even when there's
nothing wrong with them. You'll need to check your antivirus program for a
quarantine list or an "allow this file" option.

**Pressing `` ` `` does nothing.** Make sure you're actually in a match/lobby
in-game, not sitting on the main menu. Also double check you're pressing the
key next to **1**, not the apostrophe key elsewhere on the keyboard.

**I ran the loader again after a game update and it says "already patched
and up to date".** That's correct and expected — it means nothing needs to
change. Your mods are untouched either way.

**Something looks broken after uninstalling.** In Steam, right-click
Machine Party → **Properties** → **Installed Files** → **Verify integrity of
game files**. This asks Steam to check every game file against the original
and re-download anything that doesn't match. The uninstaller normally does
this for you automatically as a last resort, but it's always safe to run it
yourself too.

\---
\---

# For Mod Authors & Developers

Everything below this line assumes you're comfortable with a terminal,
Python, and/or GDScript. If you just wanted to play with mods installed,
you're done — everything you need was above.

## How it works (the whole idea in four lines)

1. The installer opens the game's `.pck` and drops in one tiny script.
2. It also flips one setting so that script runs **first**, before any game code.
3. That script looks in the `mods` folder next to the exe and loads every
   `.gd` file it finds.
4. A game update wipes the `.pck`? Run the installer again. It notices and
   re-does steps 1 and 2 by itself. The `mods` folder itself is never
   touched — it lives next to the exe/pck, not inside the pck, so a Steam
   update or "verify integrity" has no reason to touch it.

\---

## Running it from source

**Step 1.** Put `modloader_patch.py` (and `bootstrap.gd`, if you're editing
it) in the same folder as `MachineParty.exe` and `MachineParty.pck`.

**Step 2.** Open a terminal there and run:

```
python modloader_patch.py
```

It prints where the mods folder is — right next to `modloader_patch.py`
itself (`<this folder>\mods`).

**Step 3.** Start the game. If a mod doesn't seem to be loading, check
`godot.log`, which is still under Godot's per-game user data folder (not
next to the exe):

```
C:\Users\you\AppData\Roaming\Godot\app_userdata\Machine Party\logs\godot.log
```

You should see `[modloader] loaded console v1.0.0` in there.

### Building the exe

```
pip install pyinstaller
pyinstaller --onefile --name MachinePartyModLoader modloader_patch.py
```

You get one `.exe`. That's what you hand out. It has the loader script baked
inside it, so it's the only file anyone needs.

The built exe never reads a `bootstrap.gd` file sitting next to it — it
always falls back to the `BOOTSTRAP` string constant baked into
`modloader_patch.py`. If you've been hand-editing `bootstrap.gd` for
development, diff it against that constant before you build, or the exe you
hand out will silently ship stale loader logic.

The first time the built exe runs, it also drops `Uninstall Mods.bat` next
to itself (skipped when running the raw `.py` — that path is for players,
who'll only ever have the exe).

### Making it run automatically on Steam

Right-click the game → Properties → Launch Options → paste:

```
"C:\path\to\MachinePartyModLoader.exe" %command%
```

Now Steam runs the installer first and it hands off to the game. Steam keeps
launch options in its own config, so **updates don't erase this**.

\---

## Write a mod

**Step 1.** Go to the mods folder.

**Step 2.** Make a new folder. Call it whatever, e.g. `bigger_jump`.

**Step 3.** Inside it, make `mod.json`:

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

**Step 4.** Next to it, make `main.gd`:

```gdscript
extends Node

func _mod_init(loader) -> void:
	loader.note("bigger jump loading")

func _mod_ready(loader) -> void:
	loader.note("bigger jump ready")
```

**Step 5.** Start the game. Done. No packing, no rebuild.

\---

## Changing what the game already does

To replace a vanilla script, make a second file — say `override.gd` — whose very
first line points at the file you're replacing:

```gdscript
extends "res://scripts/player.gd"

func _ready() -> void:
	super._ready()
	print("player script has been modded")
```

Then in `main.gd`:

```gdscript
func _mod_init(loader) -> void:
	loader.override_script_rel("bigger_jump",
		"res://scripts/player.gd", "override.gd")
```

Because your script **extends** the original instead of deleting it, two mods
touching the same file stack on top of each other instead of one silently
winning. This is the single most important rule of the whole system.

It only affects things loaded **after** your mod runs, which is why
`_mod_init` exists and runs before the game builds anything.

\---

## Loader API (what `loader` gives you)

|Call|Does|
|-|-|
|`loader.note(text)`|print + write to `user://modloader.log`|
|`loader.compile(abs_path)`|turn a loose `.gd` file into a usable script|
|`loader.override_script(res_path, abs_path)`|swap a vanilla script|
|`loader.override_script_rel(id, res_path, rel)`|same, relative to your mod folder|
|`loader.dir_of(id)`|that mod's folder|
|`loader.has_mod(id)`|is another mod present|
|`loader.mod_list()`|everything loaded|
|`loader.open_mods_folder()`|pop the folder open in the file manager|

Mod entry points: `_mod_init(loader)` (before the game exists — do overrides
here) and `_mod_ready(loader)` (scene tree alive — safe to touch nodes).

Load order: `priority` low to high, ties broken alphabetically by `id`. A mod
listing an absent `requires` entry is skipped with a log line instead of
crashing.

**Gotcha:** `loader` (and anything else passed into your mod with no
declared type) is `Variant` to the static analyzer. `var x := loader.something()`
will fail to compile with "Cannot infer the type of X" — Godot's `:=` needs
a known type on the right side. Fix: give it an explicit type instead —
`var x: String = loader.dir_of(...)` — or drop the type and use plain
`var x = ...`. The same trap applies to typing a field as a specific class
(e.g. `var thing: Node`) and then accessing members that only exist on the
script attached to it, not on `Node` itself — leave the field untyped if you
need to reach another mod's custom signals/methods through it.

\---

## Installer flags

|Flag|Does|
|-|-|
|*(none)*|patch if needed, do nothing if already current|
|`--play`|patch, then launch the game|
|`--force`|re-patch even if up to date|
|`--restore`|put the untouched `.pck` back (keeps your mods folder and backup)|
|`--uninstall`|`--restore`, **plus** delete the mods folder, `.vanilla` backup, undo record, and `modloader.log` — asks for confirmation|
|`--yes`|skip the confirmation prompt (use with `--uninstall`)|
|`--pck` / `--exe`|explicit paths if auto-detect misses|
|`--bootstrap FILE`|inject your own edited `bootstrap.gd` (for dev)|

A clean copy is saved to `MachineParty.pck.vanilla` on the first patch (for
pack format 2 only — format 3, which this game uses, is append-only and
never needs a full-file backup), and the new pck is fully re-parsed before
anything is overwritten.

### Uninstalling, and what happens if the normal path can't restore it

`Uninstall Mods.bat` just runs `MachinePartyModLoader.exe --uninstall` and
pauses so the output stays visible. From a terminal, that's:

```
python modloader_patch.py --uninstall
```

Normal case: it restores the pck from the undo record (format 3, this
game's format — just resets a header pointer and truncates the appended
bytes) or the `.vanilla` backup (format 2), then deletes the mods folder,
the backup, the undo record, and `modloader.log`. It lists everything it's
about to do and asks for confirmation first (`--yes` skips that).

Fallback case: if there's no undo record *and* no `.vanilla` backup (e.g.
someone deleted them by hand) but the pck still has our stamp in it, there's
no local data left to restore from — so it instead tries to identify the
Steam App ID by walking up from the pck to `steamapps/appmanifest_*.acf` and
matching the install folder name, then fires `steam://validate/<appid>` to
make Steam verify and re-download the original file itself. If the App ID
can't be identified (e.g. it's not actually a Steam install), it prints
manual instructions instead: Properties → Installed Files → Verify integrity
of game files. Either way, the mods folder and logs still get cleaned up
regardless of whether the pck itself could be fixed automatically — check
the final summary line to see which case happened.

If you added the Steam launch option, remove it from the game's Properties
too; this tool can't do that part for you.

\---

## Testing checklist

Do this against a **scratch copy** of the game folder (copy `MachineParty.exe`
+ `MachineParty.pck` + the `.dll`s somewhere else), not your only install —
the tool is designed to be reversible, but there's no reason to bet on that
while you're still finding bugs in it.

### 1. The script itself

1. `python modloader_patch.py` in the scratch folder. Check the printed pack
format/version look right, and that it prints a `mods folder:` path ending in
`\mods` with the `console` example written inside it.
2. Run it again with no flags — should print `already patched and up to date`
and do nothing.
3. `python modloader_patch.py --force` — re-patches even though nothing
changed; mods folder should be untouched.
4. Launch the exe (or `--play`). In-game, press `` ` `` — the debug console
should open; try `help`, `mods`, `note hi`, `has console`, `clear`. Check
`godot.log` under Godot's `app_userdata\Machine Party\logs\` (note: that's
still in `AppData`, not next to the exe — only the mods folder moved) shows
`loaded console v1.0.0`.
5. Drop `Krunk-ChatMod` into the mods folder, relaunch. Press `T`, send a
message, toggle FILTER, mute/unmute a row, CLEAR. If you can get a second
Steam client in the lobby, spam messages to confirm the rate limit and
auto-mute actually kick in, and confirm the sender name comes from Steam (not
something a modified client could fake).
6. `python modloader_patch.py --restore` — game should launch fully vanilla
(no console, no chat). Mods folder and `.vanilla` backup should still exist.
7. `--force` to re-patch, then `python modloader_patch.py --uninstall`. Check
the printed removal list looks right, answer `y`, then confirm: pck is back to
its original size, `.vanilla` backup gone, mods folder gone,
`modloader.log` gone. Run `--uninstall` once more — it should say there's
nothing left to do.
8. Re-patch, then `--uninstall --yes` — confirms the non-interactive path
skips the prompt.
9. Delete the undo record (the `.modloader.json` file next to the pck) after
a fresh patch, then `--uninstall` again — confirms the Steam-verify fallback
path triggers instead of silently leaving the pck patched.

### 2. Building it

```
pip install pyinstaller
pyinstaller --onefile --name MachinePartyModLoader modloader_patch.py
```

### 3. The built exe

Repeat the "script itself" checklist above, but running
`dist\MachinePartyModLoader.exe` instead of `python modloader_patch.py`, in a
**fresh** scratch copy of the game folder (so you're not reusing a folder the
raw script already patched). Also check:

- It finds the `.pck`/`.exe` beside it with no `--pck`/`--exe` flags.
- Double-clicking it (not running from a terminal) leaves the window open
  with "Press Enter to close" instead of vanishing instantly — that's the
  whole reason `die()` and the end of `main()` pause when frozen.
- `Uninstall Mods.bat` appears next to it after the first run, and
  double-clicking that actually uninstalls.
- The Steam launch-option form works: `"C:\path\to\MachinePartyModLoader.exe" %command%`
  — set that on a real (or test) Steam entry, launch from Steam, confirm the
  game still starts and the console/chat mod still load, and confirm it does
  **not** pause waiting for Enter in this case (that would hang the game
  launch forever).

\---

## If something goes wrong

**"does not start with GDPC"** — the pck is embedded inside the exe. This tool
can't patch that; you'd need a different approach.

**Game won't start after patching** — run `--restore`, confirm the game launches,
then check `user://modloader.log`.

**Mod doesn't load** — check the log. Usual causes: `mod.json` has a trailing
comma, `entry` doesn't match the actual filename, or `main.gd` doesn't say
`extends Node` on line 1.

**"compile failed" for a script that looks fine** — check `user://logs/godot.log`
(not `modloader.log`) for the actual parser error; `[modloader] compile failed: ...`
only tells you *that* it failed, not *why*. See the `:=` gotcha under
[Loader API](#loader-api-what-loader-gives-you) above — that's the most common cause.

**One thing to test before shipping this to anyone:** the injected bootstrap is
plain GDScript source, while the rest of the pck is binary-tokenized. Godot's
parser sniffs the header and handles both, so it should be fine — but confirm it
on your build before you hand the tool out.
