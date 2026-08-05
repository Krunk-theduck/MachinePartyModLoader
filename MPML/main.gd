extends Node

const TOGGLE_KEY := KEY_QUOTELEFT

var _loader
var _panel: PanelContainer
var _log: RichTextLabel
var _input: LineEdit
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
	var layer := CanvasLayer.new()
	layer.layer = 50
	get_tree().root.add_child.call_deferred(layer)

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
	layer.add_child(_panel)

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

	_input = LineEdit.new()
	_input.placeholder_text = "type a command, 'help' to list them"
	_input.tooltip_text = "commands: help, mods, note <text>, has <mod_id>, open_mods, clear"
	_input.text_submitted.connect(_on_submit)
	vbox.add_child(_input)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == TOGGLE_KEY:
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
		_input.grab_focus()
	else:
		Input.mouse_mode = _prev_mouse_mode
		_input.release_focus()


func _on_submit(text: String) -> void:
	text = text.strip_edges()
	_input.text = ""
	if not text.is_empty():
		_print("> " + text)
		_run(text)
	_input.grab_focus()


func _run(line: String) -> void:
	var parts := line.split(" ", false)
	var cmd := parts[0].to_lower()
	var args := parts.slice(1)
	match cmd:
		"help":
			_print("commands: help, mods, note <text>, has <mod_id>, open_mods, clear")
		"mods":
			for m in _loader.mod_list():
				_print(" - " + m)
		"note":
			if args.is_empty():
				_print("usage: note <text>")
			else:
				_loader.note(" ".join(args))
				_print("noted")
		"has":
			if args.is_empty():
				_print("usage: has <mod_id>")
			else:
				_print(str(_loader.has_mod(args[0])))
		"open_mods":
			_loader.open_mods_folder()
		"clear":
			_log.text = ""
		_:
			_print("unknown command: " + cmd)


func _print(line: String) -> void:
	_log.append_text(line + "\n")
	_log.scroll_to_line(_log.get_line_count())
