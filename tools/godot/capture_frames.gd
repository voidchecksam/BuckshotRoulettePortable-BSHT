# Real frames from the original game, for comparing the port's --shot frames
# against what Godot 4.1.4 draws (icons, labels, materials, layout).
#
# Runs the same round-1 ceremony setup as dump_state.gd's ceremony_b1r1 on the
# start frame (seed, waiver signed, camera "home", intro manager freed as a
# diagnostic boot skips the intro, then RoundManager.MainBatchSetup(true)) and
# saves the viewport at each requested frame.
#
# Needs the windowed binary (the headless one has a dummy renderer), so run it
# through tools/godot/capture_reference.sh:
#   tools/godot/capture_reference.sh 363,543,663,783 build/shots/godot
# Frame N here matches the port's `--shot-delay (N - 3) / 60` (the port starts
# the ceremony on its first frame; this starts it on frame 3).
extends SceneTree

const START_FRAME := 3
const SEED := 20260915

var _root: Node = null
var _frame := 0
var _targets: Array = []
var _out_dir := ""

# Optional third argument, the port's --autoplay-input as frames:
#   "FRAME:key:NAME"    the key pressed at FRAME, released at FRAME+1
#   "FRAME:move:X,Y"    the pointer moved to viewport X,Y
#   "FRAME:click:X,Y"   left button pressed there, released at FRAME+1
var _presses := {}    # frame -> Array of keycodes
var _releases := {}
var _moves := {}      # frame -> Array of Vector2
var _clicks := {}
var _click_releases := {}
var _last_focus := ""
var _pointer := Vector2.ZERO

const KEYS := {"up": KEY_UP, "down": KEY_DOWN, "left": KEY_LEFT, "right": KEY_RIGHT, "enter": KEY_ENTER, "escape": KEY_ESCAPE}

func _key(code: int, pressed: bool) -> InputEventKey:
	var k := InputEventKey.new()
	k.keycode = code
	k.pressed = pressed
	k.device = 0
	return k

func _move(to: Vector2) -> InputEventMouseMotion:
	var m := InputEventMouseMotion.new()
	m.position = to
	m.global_position = to
	m.relative = to - _pointer
	m.device = 0
	_pointer = to
	return m

func _click(at: Vector2, pressed: bool) -> InputEventMouseButton:
	var b := InputEventMouseButton.new()
	b.button_index = MOUSE_BUTTON_LEFT
	b.pressed = pressed
	b.position = at
	b.global_position = at
	b.button_mask = MOUSE_BUTTON_MASK_LEFT if pressed else 0
	b.device = 0
	return b

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2 and args.size() != 3:
		printerr("usage: -- <frame,frame,...> <out dir under the project> [FRAME:key:NAME,...]")
		quit(2); return
	for s in String(args[0]).split(","):
		_targets.append(int(s))
	if args.size() == 3:
		# A pointer entry carries its own comma ("F:move:480,270"), so a chunk
		# that starts one takes the next chunk as its Y.
		var chunks := String(args[2]).split(",")
		var i := 0
		while i < chunks.size():
			var entry: String = chunks[i]
			var parts := entry.split(":")
			if parts.size() != 3:
				printerr("bad input entry: ", entry)
				quit(2); return
			var f := int(parts[0])
			var what: String = parts[1]
			if what == "key":
				if not KEYS.has(parts[2]):
					printerr("bad key: ", entry)
					quit(2); return
				_presses[f] = _presses.get(f, []) + [KEYS[parts[2]]]
				_releases[f + 1] = _releases.get(f + 1, []) + [KEYS[parts[2]]]
				i += 1
			elif what == "move" or what == "click":
				if i + 1 >= chunks.size():
					printerr("pointer entry needs X,Y: ", entry)
					quit(2); return
				var at := Vector2(float(parts[2]), float(chunks[i + 1]))
				if what == "move":
					_moves[f] = _moves.get(f, []) + [at]
				else:
					_clicks[f] = _clicks.get(f, []) + [at]
					_click_releases[f + 1] = _click_releases.get(f + 1, []) + [at]
				i += 2
			else:
				printerr("bad input entry: ", entry)
				quit(2); return
	_out_dir = args[1] if args[1].is_absolute_path() and not args[1].begins_with("res://") else ProjectSettings.globalize_path("res://" + args[1])
	DirAccess.make_dir_recursive_absolute(_out_dir)
	# The translations load, but their locale codes are the sheet's ("EN"), so
	# the OS locale en_US matches none and tr() returns raw keys ("LIVEROUND").
	# The port reads the EN strings. BSHT_LOCALE=<sheet code> (FR, RU, JA...)
	# captures another language, for a glyph comparison.
	var locale := OS.get_environment("BSHT_LOCALE")
	TranslationServer.set_locale(locale if locale != "" else "EN")
	seed(0)
	_root = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	get_root().add_child(_root)

func _process(_delta: float) -> bool:
	_frame += 1
	for code in _releases.get(_frame, []):
		Input.parse_input_event(_key(code, false))
	for at in _click_releases.get(_frame, []):
		Input.parse_input_event(_click(at, false))
	for at in _moves.get(_frame, []):
		Input.parse_input_event(_move(at))
		print("input frame ", _frame, ": move ", at)
	for at in _clicks.get(_frame, []):
		Input.parse_input_event(_move(at))
		Input.parse_input_event(_click(at, true))
		print("input frame ", _frame, ": click ", at)
	for code in _presses.get(_frame, []):
		Input.parse_input_event(_key(code, true))
		print("input frame ", _frame, ": key ", code)
	if not _presses.is_empty():
		var owner = get_root().gui_get_focus_owner()
		var desc := "%s cursor.controller_active=%s" % [owner.get_path() if owner else "none",
			_root.get_node("standalone managers/cursor manager").controller_active]
		if desc != _last_focus:
			_last_focus = desc
			print("focus frame ", _frame, ": ", desc)
	if _frame == START_FRAME:
		seed(SEED)
		var rm = _root.get_node("standalone managers/round manager")
		rm.playerData.hasSignedWaiver = true
		var cm = _root.get_node("standalone managers/camera manager")
		for s in cm.socketArray:
			if s.socketName == "home":
				cm.cam.transform.origin = s.pos
				cm.cam.rotation_degrees = s.rot
				cm.cam.fov = s.fov
		# IntroManager.gd hides its full-screen viewblocker (L85/L118) as the
		# intro plays; freed here, so hide it as the intro would have.
		_root.get_node("Camera/dialogue UI/viewblocker parent/viewblcoker").visible = false
		# The walk to the table ("camera enter backroom", IntroManager L276)
		# ends with this lamp at 0, and nothing turns it on again: every round
		# is played without it. Skipping the intro left it at its load energy.
		_root.get_node("light parent/lght_backroom main2").light_energy = 0.0
		_root.get_node("standalone managers/intro manager").queue_free()
		rm.MainBatchSetup(true)
	if _targets.has(_frame):
		# The viewport texture holds the last drawn frame.
		var img: Image = get_root().get_texture().get_image()
		var path := "%s/godot_f%d.png" % [_out_dir, _frame]
		img.save_png(path)
		print("wrote ", path)
	if _targets.size() > 0 and _frame >= int(_targets.max()) + 1:
		quit(0)
		return true
	return false
