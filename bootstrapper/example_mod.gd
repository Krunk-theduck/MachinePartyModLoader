extends Node

const TOGGLE_KEY := KEY_QUOTELEFT

var _loader
var _layer: CanvasLayer
var _panel: PanelContainer
var _log: RichTextLabel
var _inputEdit: LineEdit
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
	_layer = CanvasLayer.new()
	_layer.layer = 50

	# Keep the UI under this mod so it is cleaned up during soft reload.
	add_child(_layer)

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
	_layer.add_child(_panel)

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

	_inputEdit = LineEdit.new()
	_inputEdit.placeholder_text = "type a command, 'help' to list them"
	_inputEdit.tooltip_text = "Type help for commands"
	_inputEdit.text_submitted.connect(_on_submit)
	vbox.add_child(_inputEdit)


# Use _input instead of _unhandled_input so the toggle key is caught
# before the LineEdit can type it.
func _input(event: InputEvent) -> void:
	if event is InputEventKey:
		if event.pressed and not event.echo and event.keycode == TOGGLE_KEY:
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
		_inputEdit.grab_focus()
	else:
		Input.mouse_mode = _prev_mouse_mode
		_inputEdit.release_focus()


func _on_submit(text: String) -> void:
	text = text.strip_edges()
	_inputEdit.text = ""

	if not text.is_empty():
		_print("> " + text)
		_run(text)

	if is_instance_valid(_inputEdit):
		_inputEdit.grab_focus()


func _run(line: String) -> void:
	var parts := line.split(" ", false)
	if parts.is_empty():
		return

	var cmd := parts[0].to_lower()
	var args := parts.slice(1)

	match cmd:
		"help":
			_print("Available commands:")
			_print("  help                  - show this list")
			_print("  mods                  - list loaded mods")
			_print("  plugins               - list loaded plugins")
			_print("  has <id>              - check if a mod is loaded")
			_print("  has_plugin <id>       - check if a plugin is loaded")
			_print("  has_api <name>        - check if an API method exists")
			_print("  note <text>           - write to the loader log")
			_print("  open_mods             - open the mods folder")
			_print("  open_plugins          - open the plugins folder")
			_print("  version               - show loader and API versions")
			_print("  clear                 - clear console output")

		"mods":
			var list: Array = _loader.mod_list()
			if list.is_empty():
				_print("No mods loaded.")
			else:
				_print("Loaded mods:")
				for m in list:
					_print(" - " + str(m))

		"plugins":
			var list: Array = _loader.plugin_list()
			if list.is_empty():
				_print("No plugins loaded.")
			else:
				_print("Loaded plugins:")
				for p in list:
					_print(" - " + str(p))

		"has":
			if args.is_empty():
				_print("usage: has <mod_id>")
			else:
				_print(str(_loader.has_mod(args[0])))

		"has_plugin":
			if args.is_empty():
				_print("usage: has_plugin <plugin_id>")
			else:
				_print(str(_loader.has_plugin(args[0])))

		"has_api":
			if args.is_empty():
				_print("usage: has_api <method_name>")
			else:
				_print(str(_loader.has_api(args[0])))

		"note":
			if args.is_empty():
				_print("usage: note <text>")
			else:
				_loader.note(" ".join(args))
				_print("noted")

		"open_mods":
			_loader.open_mods_folder()
			_print("opened mods folder")

		"open_plugins":
			_loader.open_plugins_folder()
			_print("opened plugins folder")

		"version":
			_print("Loader version: " + _loader.loader_version())
			_print("API version: " + str(_loader.api_version()))

		"clear":
			_log.clear()

		_:
			_print("unknown command: " + cmd + " (try 'help')")


func _print(line: String) -> void:
	if not is_instance_valid(_log):
		return

	_log.append_text(line + "\n")
	_log.scroll_to_line(_log.get_line_count())