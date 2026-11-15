extends Node
# Loose scripts in gd/ (next to the game exe) load automatically - no mod.json
# needed. Scripts extending Node are started like mods; every other script is
# just compiled and registered. Delete this file whenever you like.

func _mod_init(loader) -> void:
        loader.note("gd/example.gd loaded - loose scripts work")
