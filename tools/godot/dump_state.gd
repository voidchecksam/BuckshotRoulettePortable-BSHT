# Run a parity scenario against the real engine and write the state dump that
# tests/parity/bsht_parity diffs the C++ runtime against.
#
#   godot --headless --fixed-fps 60 --path <game> --script res://tools/godot/dump_state.gd -- \
#       <scenario.json> [--ref-dir=<dir>] [--shots-dir=<dir>]
#
# Paths may be absolute (the reference game: tools/godot/ref_game.sh) or
# res:// relative. Output: <ref-dir>/<scenario>.json, default
# res://assets/reference (gitignored, never packaged).
extends SceneTree

# Read by Probe through get_tree().get(): GDScript's analyzer does not know a
# native SceneTree has script members.
var frame := 0
var events: Array = []
var _scenario: Dictionary = {}
var _root: Node = null
var _ref_dir := "res://assets/reference"
var _shots_dir := "res://build/shots"

# An absolute path stays as it is; anything else is under res://.
func _abs(path: String) -> String:
	if path.is_absolute_path() and not path.begins_with("res://"):
		return path
	return ProjectSettings.globalize_path(path if path.begins_with("res://") else "res://" + path)

func _shot_path(name: String, frame_no: int) -> String:
	return _abs(_shots_dir).path_join("godot_%s_%d.png" % [name, frame_no])

# The frame_order probe. tests/parity/probes.cpp is its C++ twin; change both.
class Probe extends Node:
	func tree_frame() -> int:
		return int(get_tree().get("frame"))
	func log_event(s: String) -> void:
		get_tree().get("events").append("f%d:%s" % [tree_frame(), s])
	func _ready() -> void:
		log_event("ready:" + String(name))
		if String(name) == "A":
			wait("t40", 0.04)
	func _process(delta: float) -> void:
		if String(name) == "A" or String(name) == "D":
			log_event("process:%s:%.3f" % [String(name), delta * 1000.0])
		if String(name) == "A" and tree_frame() == 4:
			Engine.time_scale = 0.5
			wait("t20", 0.02)
	func wait(tag: String, t: float) -> void:
		await get_tree().create_timer(t, false).timeout
		log_event("timer:" + tag)
		if tag == "t40":
			await get_tree().create_timer(0.0, false).timeout
			log_event("timer:zero")

# The anim_play_semantics probe. tests/parity/probes.cpp RunAnimPlaySemanticsProbe
# is its C++ twin; change both. P runs the scenario's steps and logs "pre" in its
# _process (before AP, which follows it in tree order); Z logs "post" after AP
# has processed. AP's METHOD keys call P.mark().
class SemProbe extends Node:
	var steps: Array = []
	var ap: AnimationPlayer = null
	var t: Node3D = null
	var is_post := false
	func tree_frame() -> int:
		return int(get_tree().get("frame"))
	func log_event(s: String) -> void:
		get_tree().get("events").append("f%d:%s" % [tree_frame(), s])
	func state(tag: String) -> void:
		log_event("%s:x=%.4f,sx=%.4f,sy=%.4f,vis=%d,playing=%d,cur=%s,pos=%.4f" % [
			tag, t.position.x, t.scale.x, t.scale.y, int(t.visible), int(ap.is_playing()),
			ap.current_animation, ap.current_animation_position])
	func _process(_delta: float) -> void:
		if is_post:
			state("post")
			return
		for s in steps:
			if int(s["frame"]) != tree_frame():
				continue
			match String(s["do"]):
				"play":
					ap.play(String(s["anim"]))
				"stop":
					ap.stop()
				"seek":
					ap.seek(float(s["time"]), bool(s["update"]))
		state("pre")
	func mark(tag: String) -> void:
		log_event("mark:" + tag)

func _initialize() -> void:
	# The 17 translations DO load, but their locales are the sheet's own codes
	# ("EN", not "en"), so the OS locale en_US matches none and tr() returns the
	# raw key ("LIVEROUND" for "LIVE ROUND."). The game sets its locale itself;
	# the port reads the EN strings, so compare like with like.
	TranslationServer.set_locale("EN")
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		printerr("usage: -- <scenario.json>")
		quit(2); return
	for a in args.slice(1):
		if a.begins_with("--ref-dir="): _ref_dir = a.trim_prefix("--ref-dir=")
		elif a.begins_with("--shots-dir="): _shots_dir = a.trim_prefix("--shots-dir=")
	var text := FileAccess.get_file_as_string(_abs(args[0]))
	var parsed = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		printerr("FAIL: bad scenario ", args[0])
		quit(2); return
	_scenario = parsed
	# Input scenarios run in a real window, and the OS delivers the physical
	# cursor to it: MouseRaycast.mouse took the desk cursor's (647, 329) at the
	# gate and again mid-run (in use_magnifier_b2r1), so a tap picked a
	# different item than the port did. Mouse passthrough keeps the desk cursor
	# out, so the only pointer events are the timeline's. (Moving the window
	# off-screen does too, but Windows then stops presenting it and the "shots"
	# come back stale.) The port has no OS cursor to shut out.
	if _scenario.has("timeline"):
		DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_MOUSE_PASSTHROUGH, true)
	# Item-use scenarios load main.tscn exactly as items_b2r1 does; one rule
	# for the whole family instead of a name per scenario.
	# "frames_<name>": the same scenario run only for its "shots" (frame
	# checks); it writes its own reference file, so the parity one is untouched.
	var family := String(_scenario["name"]).trim_prefix("frames_")
	if family.begins_with("use_") or family.begins_with("dealer_"):
		family = "items_b2r1"
	if family.begins_with("menu_"):
		_begin_menu()
		return
	if family.begins_with("intro_"):
		_begin_intro()
		return
	if family.begins_with("ending_"):
		_begin_ending()
		return
	if family.begins_with("heaven_"):
		_begin_heaven()
		return
	if family.begins_with("waiver_"):
		# The waiver reads and writes user:// (the pills gate, the main save):
		# only in the private parity directory, as the menu does.
		if not _private_user_dir():
			return
		family = "items_b2r1"
	match family:
		"rng":
			_run_rng()
		"anim_sample":
			_run_anim_sample()
		"anim_play_semantics":
			_build_semantics()
		"ceremony_b1r1":
			seed(0)
			_root = (load("res://scenes/main.tscn") as PackedScene).instantiate()
			get_root().add_child(_root)
		"anim_main_shoot":
			seed(0)
			_root = (load("res://scenes/main.tscn") as PackedScene).instantiate()
			get_root().add_child(_root)
		"grab_shotgun_b1r1", "grab_shotgun_touch_b1r1", "grab_shotgun_quiet", "shoot_dealer_b1r1", "shoot_self_b1r1", \
		"items_b2r1":
			seed(0)
			_root = (load("res://scenes/main.tscn") as PackedScene).instantiate()
			get_root().add_child(_root)
		"load_main":
			seed(0)
			_root = (load("res://scenes/main.tscn") as PackedScene).instantiate()
			get_root().add_child(_root)
		"frame_order":
			var a := _probe("A")
			var b := _probe("B")
			a.add_child(b)
			b.add_child(_probe("D"))
			a.add_child(_probe("C"))
			get_root().add_child(a)
		_:
			printerr("FAIL: unknown scenario")
			quit(2)

func _build_semantics() -> void:
	var p := SemProbe.new()
	p.name = "P"
	var ap := AnimationPlayer.new()
	ap.name = "AP"
	var t := Node3D.new()
	t.name = "T"
	var z := SemProbe.new()
	z.name = "Z"
	z.is_post = true
	p.add_child(ap)
	p.add_child(t)
	p.add_child(z)
	var lib := AnimationLibrary.new()
	var data: Dictionary = _scenario["animations"]["players"][0]
	for a in data["animations"]:
		lib.add_animation(String(a["name"]), _build_animation(a))
	ap.add_animation_library("", lib)
	# The C++ side resolves against an absolute root "."; here that is P.
	ap.root_node = NodePath("..")
	p.ap = ap
	p.t = t
	z.ap = ap
	z.t = t
	p.steps = _scenario["steps"]
	get_root().add_child(p)

# An Animation from the animations.json v2 shape the C++ side loads.
func _build_animation(a: Dictionary) -> Animation:
	var anim := Animation.new()
	anim.length = float(a["length"])
	anim.loop_mode = int(a["loop"])
	for tr in a["tracks"]:
		var type := int(tr["type"])
		var idx := anim.add_track(type)
		anim.track_set_path(idx, NodePath(String(tr["path"])))
		anim.track_set_interpolation_type(idx, int(tr["interp"]))
		anim.track_set_interpolation_loop_wrap(idx, bool(tr["loop_wrap"]))
		if type == Animation.TYPE_VALUE:
			anim.value_track_set_update_mode(idx, int(tr["update"]))
		for k in tr["keys"]:
			var value = null
			if type == Animation.TYPE_METHOD:
				var args: Array = []
				for arg in k["a"]:
					args.append(_json_value(arg))
				value = {"method": String(k["m"]), "args": args}
			else:
				value = _json_value(k)
			anim.track_insert_key(idx, float(k["t"]), value, float(k["tr"]))
	return anim

func _json_value(d: Dictionary):
	var v = d["v"]
	match String(d["k"]):
		"b":
			return bool(v)
		"i":
			return int(v)
		"f":
			return float(v)
		"s":
			return String(v)
		"v2":
			return Vector2(v[0], v[1])
		"v3":
			return Vector3(v[0], v[1], v[2])
		"c":
			return Color(v[0], v[1], v[2], v[3])
		"q":
			return Quaternion(v[0], v[1], v[2], v[3])
	return null

func _probe(n: String) -> Probe:
	var p := Probe.new()
	p.name = n
	return p

func _process(_delta: float) -> bool:
	frame += 1
	var scenario_name := String(_scenario.get("name", "")).trim_prefix("frames_")
	if scenario_name == "load_main" and frame == int(_scenario["capture_frame"]):
		_write({"nodes": _capture_nodes()})
		return true
	if scenario_name == "anim_main_shoot":
		return _step_main_shoot()
	if scenario_name == "ceremony_b1r1":
		return _step_ceremony()
	if scenario_name.begins_with("menu_"):
		return _step_menu()
	if scenario_name.begins_with("intro_"):
		return _step_intro()
	if scenario_name.begins_with("ending_"):
		return _step_ending()
	if scenario_name.begins_with("heaven_"):
		return _step_heaven()
	if scenario_name.begins_with("grab_shotgun") or scenario_name.begins_with("shoot_") \
			or scenario_name.begins_with("items_") or scenario_name.begins_with("use_") \
			or scenario_name.begins_with("dealer_") or scenario_name.begins_with("waiver_"):
		return _step_grab()
	if scenario_name == "anim_play_semantics" and frame > int(_scenario["frames"]):
		_write({"events": events})
		return true
	if scenario_name == "frame_order" and frame > int(_scenario["frames"]):
		_write({"events": events})
		return true
	return false

# anim_main_shoot: a real AnimationPlayer in main.tscn plays a real animation.
# Each frame, before nodes process (this is SceneTree._process), the watched
# nodes are sampled and watched speakers that just started playing are logged;
# then, on start_frame, the animation is played. tests/parity/parity_main.cpp
# RunMainShoot does the same around process_frame. Only watched nodes: the
# scene's own scripts keep running here and may move anything else.
var _audio_seen := {}
var _shoot_samples: Array = []

func _step_main_shoot() -> bool:
	var start := int(_scenario["start_frame"])
	if frame >= start:
		for path in _scenario["audio_watch"]:
			var a = _root.get_node_or_null(NodePath(String(path)))
			if a != null and a.playing and not _audio_seen.has(path):
				_audio_seen[path] = true
				events.append("f%d:audio:%s" % [frame, path])
		if (frame - start) % int(_scenario["every"]) == 0:
			for path in _scenario["watch"]:
				var n: Node3D = _root.get_node(NodePath(String(path)))
				_shoot_samples.append({"id": "f%d|%s" % [frame, path], "v": _watch_numbers(n)})
	if frame >= start + int(_scenario["frames"]):
		_write({"events": events, "samples": _shoot_samples})
		return true
	if frame == start:
		var player: AnimationPlayer = _root.get_node(NodePath(String(_scenario["player"])))
		player.play(String(_scenario["animation"]))
	return false

# ceremony_b1r1: the round-1 ceremony from the game's own scripts.
# tests/parity/parity_main.cpp RunCeremony is its C++ twin; change both.
# On start_frame (before nodes process): seed, sign the waiver, snap the
# camera to "home", call RoundManager.MainBatchSetup(true). Every frame from
# then, watched AnimationPlayers log current_animation changes; every `every`
# frames the camera, dialogue box, compartment shells, health counter and
# ShellSpawner.sequenceArray are sampled.
var _cer_anim := {}
var _cer_samples: Array = []

func _step_ceremony() -> bool:
	var start := int(_scenario["start_frame"])
	if frame == start and bool(_scenario.get("quiet_rng", false)):
		# Diagnostic, matching bsht_parity: silence the blinker/TextShaker loops
		# so the rng probe measures the ceremony's own draws.
		_quiet_rng(_root)
	if frame >= start:
		for path in _scenario["players"]:
			var ap: AnimationPlayer = _root.get_node(NodePath(String(path)))
			var cur := String(ap.current_animation)
			if frame > start and _cer_anim.get(path, "") != cur:
				events.append("f%d:anim:%s:%s" % [frame, path, cur])
			_cer_anim[path] = cur
		if (frame - start) % int(_scenario["every"]) == 0:
			_cer_samples.append_array(_ceremony_samples())
	if frame >= start + int(_scenario["frames"]):
		_write({"events": events, "samples": _cer_samples})
		return true
	if frame == start:
		seed(int(_scenario["seed"]))
		var rm = _root.get_node("standalone managers/round manager")
		rm.playerData.hasSignedWaiver = true
		var cm = _root.get_node("standalone managers/camera manager")
		for s in cm.socketArray:
			if s.socketName == "home":
				cm.cam.transform.origin = s.pos
				cm.cam.rotation_degrees = s.rot
				cm.cam.fov = s.fov
		# The restroom intro (IntroManager.gd) moves the camera 0.5 s after
		# _ready; freed here, as a diagnostic boot skips the intro.
		_root.get_node("standalone managers/intro manager").queue_free()
		rm.MainBatchSetup(true)
	return false

func _ceremony_samples() -> Array:
	var out: Array = []
	var cam: Camera3D = _root.get_node("Camera")
	var o: Vector3 = cam.global_transform.origin
	out.append({"id": "f%d|cam" % frame, "v": [o.x, o.y, o.z, cam.fov]})
	var lbl: Label = _root.get_node("Camera/dialogue UI/main dialogue/dialogue text")
	var bd: ColorRect = _root.get_node("Camera/dialogue UI/main dialogue/dialogue backdrop")
	out.append({"id": "f%d|dialogue" % frame, "v": [1.0 if lbl.visible else 0.0,
		float(lbl.visible_characters), float(lbl.text.length()), 1.0 if bd.visible else 0.0]})
	var comp: Node = _root.get_node("tabletop parent/main tabletop/compartment_shells")
	var sv: Array = []
	for c in comp.get_children():
		if c is MeshInstance3D:
			var p: Vector3 = c.global_transform.origin
			var col: Color = c.get_active_material(1).albedo_color
			sv.append_array([p.x, p.y, p.z, col.r, col.g, col.b])
	out.append({"id": "f%d|shells" % frame, "v": sv})
	var hc = _root.get_node("standalone managers/health counter")
	var rm = _root.get_node("standalone managers/round manager")
	var cv: Array = []
	for s in hc.symbolArray_player:
		cv.append(1.0 if s.visible else 0.0)
	for s in hc.symbolArray_enemy:
		cv.append(1.0 if s.visible else 0.0)
	for u in hc.uiArray:
		cv.append(1.0 if u.visible else 0.0)
	cv.append_array([1.0 if rm.roundIndicatorParent.visible else 0.0, 1.0 if rm.roundIndicator.visible else 0.0,
		float(rm.health_player), float(rm.health_opponent)])
	out.append({"id": "f%d|counter" % frame, "v": cv})
	# The round indicator's glow sits inside a ring of its parent's texture; the
	# owner saw it off-centre. Global basis + origin of both.
	var iv: Array = []
	for n in [rm.roundIndicatorParent, rm.roundIndicator]:
		var g: Transform3D = n.global_transform
		iv.append_array([g.basis.x.x, g.basis.x.y, g.basis.x.z, g.basis.y.x, g.basis.y.y, g.basis.y.z,
			g.basis.z.x, g.basis.z.y, g.basis.z.z, g.origin.x, g.origin.y, g.origin.z])
	out.append({"id": "f%d|indicator" % frame, "v": iv})
	var q: Array = []
	for s in _root.get_node("standalone managers/shell spawner").sequenceArray:
		q.append(1.0 if s == "live" else 0.0)
	out.append({"id": "f%d|sequence" % frame, "v": q})
	return out

# Global basis and origin, visible in tree, then surface-0 albedo alpha when the
# active material is a BaseMaterial3D.
func _watch_numbers(n: Node3D) -> Array:
	var g := n.global_transform
	var out := [g.basis.x.x, g.basis.x.y, g.basis.x.z, g.basis.y.x, g.basis.y.y, g.basis.y.z,
		g.basis.z.x, g.basis.z.y, g.basis.z.z, g.origin.x, g.origin.y, g.origin.z,
		1.0 if n.is_visible_in_tree() else 0.0]
	if n is MeshInstance3D and n.mesh != null and n.mesh.get_surface_count() > 0:
		var m = n.get_active_material(0)
		if m is BaseMaterial3D:
			out.append(m.albedo_color.a)
	return out

func _run_rng() -> void:
	seed(int(_scenario["seed"]))
	var out: Array = []
	for i in int(_scenario["repeat"]):
		for op in _scenario["ops"]:
			match String(op[0]):
				"randi_range":
					out.append(randi_range(int(op[1]), int(op[2])))
				"randf_range":
					out.append(randf_range(float(op[1]), float(op[2])))
				"randf":
					out.append(randf())
				"randi":
					out.append(randi())
	_write({"rng": out})

# Every track of every animation in main.tscn, sampled by the engine's own
# Animation methods at fixed fractions of its length and just past the end.
# tests/parity/parity_main.cpp RunAnimSample produces the same ids.
func _run_anim_sample() -> void:
	var root: Node = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	var players: Array = []
	_collect_players(root, root, players)
	var samples: Array = []
	for pp in players:
		var player: AnimationPlayer = pp["node"]
		for anim_name in player.get_animation_list():
			var anim: Animation = player.get_animation(anim_name)
			var times: Array = []
			for f in _scenario["fractions"]:
				times.append(anim.length * float(f))
			times.append(anim.length + float(_scenario["past_end"]))
			for t in anim.get_track_count():
				for ti in times.size():
					var nums := _numbers(_sample_track(anim, t, times[ti]))
					if nums.is_empty():
						continue
					samples.append({"id": "%s|%s|%d|%d" % [pp["path"], anim_name, t, ti], "v": nums})
	root.free()
	_write({"samples": samples})

func _sample_track(anim: Animation, t: int, time: float):
	match anim.track_get_type(t):
		Animation.TYPE_VALUE:
			return anim.value_track_interpolate(t, time)
		Animation.TYPE_POSITION_3D:
			return anim.position_track_interpolate(t, time)
		Animation.TYPE_ROTATION_3D:
			return anim.rotation_track_interpolate(t, time)
		Animation.TYPE_SCALE_3D:
			return anim.scale_track_interpolate(t, time)
	return null

func _collect_players(n: Node, root: Node, out: Array) -> void:
	if n is AnimationPlayer:
		out.append({"node": n, "path": String(root.get_path_to(n))})
	for c in n.get_children():
		_collect_players(c, root, out)

# The numbers the C++ side compares. Anything else (strings, nil) is not sampled.
func _numbers(v) -> Array:
	match typeof(v):
		TYPE_BOOL:
			return [1.0 if v else 0.0]
		TYPE_INT, TYPE_FLOAT:
			return [float(v)]
		TYPE_VECTOR2:
			return [v.x, v.y]
		TYPE_VECTOR3:
			return [v.x, v.y, v.z]
		TYPE_QUATERNION:
			return [v.x, v.y, v.z, v.w]
		TYPE_COLOR:
			return [v.r, v.g, v.b, v.a]
	return []

func _capture_nodes() -> Array:
	var nodes: Array = []
	_walk(_root, nodes)
	return nodes

func _walk(n: Node, nodes: Array) -> void:
	if n is Node3D:
		var g: Transform3D = n.global_transform
		var e := {
			"path": String(_root.get_path_to(n)),
			"g": [g.basis.x.x, g.basis.x.y, g.basis.x.z, g.basis.y.x, g.basis.y.y, g.basis.y.z,
				  g.basis.z.x, g.basis.z.y, g.basis.z.z, g.origin.x, g.origin.y, g.origin.z],
			"v": n.visible,
			"vt": n.is_visible_in_tree(),
		}
		if n is VisualInstance3D:
			e["l"] = n.layers
		if n is GeometryInstance3D:
			e["t"] = n.transparency
		# Surface 0's active material albedo: the muzzle-flash cone is hidden by
		# its alpha alone. BaseMaterial3D only, as export_runtime_scene.gd.
		if n is MeshInstance3D and n.mesh != null and n.mesh.get_surface_count() > 0:
			var m = n.get_active_material(0)
			if m is BaseMaterial3D:
				var c: Color = m.albedo_color
				e["a"] = [c.r, c.g, c.b, c.a]
		nodes.append(e)
	for c in n.get_children():
		_walk(c, nodes)

# grab_shotgun_b1r1 / grab_shotgun_touch_b1r1: the player picks the gun up and
# chooses, by keyboard or by touch. tests/parity/parity_main.cpp RunGrab is the
# C++ twin; change both.
#
# The ceremony runs first (the same setup ceremony_b1r1 uses), because the desk
# UI only appears when RoundManager reaches the player's turn. `at` in the
# timeline counts from the frame `wait_for_visible` first becomes visible, so
# neither side depends on how many frames the ceremony took.
var _grab_samples_out: Array = []
var _grab_zero := -1
var _grab_touch_pos := {}

func _step_grab() -> bool:
	var start := int(_scenario["start_frame"])
	if frame < start:
		return false
	if frame == start:
		seed(int(_scenario["seed"]))
		var rm = _root.get_node("standalone managers/round manager")
		rm.playerData.hasSignedWaiver = not bool(_scenario.get("waiver", false))
		# batch: which batch MainBatchSetup starts, as the port's --batch does.
		rm.playerData.currentBatchIndex = int(_scenario.get("batch", 0))
		var cm = _root.get_node("standalone managers/camera manager")
		for s in cm.socketArray:
			if s.socketName == "home":
				cm.cam.transform.origin = s.pos
				cm.cam.rotation_degrees = s.rot
				cm.cam.fov = s.fov
		_root.get_node("standalone managers/intro manager").queue_free()
		# The intro's blackout, as app::ScenarioRunner and Session::Start hide
		# it. Visual only: "shots" would be black without it.
		_root.get_node("Camera/dialogue UI/viewblocker parent/viewblcoker").visible = false
		# The walk to the table ("camera enter backroom", IntroManager L276) ends
		# with this lamp at 0 for the rest of the game; skipping the intro left
		# it at its load energy, so "shots" were lit by it. Visual only: light
		# energy is not in the dump. (capture_frames.gd does the same.)
		_root.get_node("light parent/lght_backroom main2").light_energy = 0.0
		# Diagnostic (quiet_rng): stop everything else that draws from the global
		# RNG (the BlinkerBranch loops and TextShaker), so a difference in a shake
		# value can only be the shake's own draw, not the interleaving.
		# ObjectShaker keeps running: it uses `shaking`, not `looping`.
		if bool(_scenario.get("quiet_rng", false)):
			_quiet_rng(_root)
		rm.MainBatchSetup(true)
		return false

	if _grab_zero < 0:
		# Visible in the tree, checked up the parents: the desk bracket is `visible` at load
		# and only off screen because its parent is hidden, so gating on the
		# flag alone fires on frame 4 instead of the player's turn.
		var gate: Control = _root.get_node(NodePath(String(_scenario["wait_for_visible"])))
		if not gate.is_visible_in_tree():
			return false
		_grab_zero = frame
		# don_items: app::ScenarioRunner's twin. The Double or Nothing item pool
		# (EndlessMode.gd:25), set at the gate: MainBatchSetup parses the base
		# amounts only after awaiting RoundIndicator.
		if bool(_scenario.get("don_items", false)):
			var rmd = _root.get_node("standalone managers/round manager")
			for res in rmd.itemManager.amounts.array_amounts:
				res.amount_active = res.amount_don
		# opponent_health: app::ScenarioRunner's twin. The dealer's health at the
		# gate, so one live shot ends him (BloodSplatter, the time shift).
		if int(_scenario.get("opponent_health", -1)) >= 0:
			_root.get_node("standalone managers/round manager").health_opponent = int(_scenario["opponent_health"])
		# first_shell: "live" or "blank" in the chamber at the gate (the twin).
		if String(_scenario.get("first_shell", "")) != "":
			_root.get_node("standalone managers/round manager").shellSpawner.sequenceArray[0] = String(_scenario["first_shell"])
		# Diagnostic (reseed_at_gate): put both engines on the same RNG stream
		# position at the player's turn, so a difference afterwards can only
		# come from draws made DURING the grab, not from the ceremony before it.
		if bool(_scenario.get("reseed_at_gate", false)):
			seed(int(_scenario["seed"]))
		print("player turn reached at frame ", frame)

	var at := frame - _grab_zero
	# "shots": the viewport read here holds LAST frame's render, so the image of
	# frame `at` is saved one frame later. The port saves the same at, after it
	# drew it (bsht_desktop --scenario).
	for shot in _scenario.get("shots", []):
		if int(shot) == at - 1:   # JSON numbers are floats; `in` compares types
			var img := get_root().get_texture().get_image()
			img.save_png(_shot_path(_scenario["name"], at - 1))
	for step in _scenario["timeline"]:
		if int(step["at"]) != at:
			continue
		match String(step["event"]):
			"key":
				var k := InputEventKey.new()
				k.keycode = int(step["keycode"])
				k.pressed = bool(step["pressed"])
				k.device = 0
				Input.parse_input_event(k)
			"touch":
				var t := InputEventScreenTouch.new()
				t.index = int(step["index"])
				t.position = Vector2(float(step["x"]), float(step["y"]))
				t.pressed = bool(step["pressed"])
				Input.parse_input_event(t)
				_grab_touch_pos[t.index] = t.position
			"drag":
				var d := InputEventScreenDrag.new()
				d.index = int(step["index"])
				d.position = Vector2(float(step["x"]), float(step["y"]))
				d.relative = d.position - _grab_touch_pos.get(d.index, d.position)
				Input.parse_input_event(d)
				_grab_touch_pos[d.index] = d.position

	var sn := String(_scenario["name"]).trim_prefix("frames_")
	if sn.begins_with("waiver_"):
		_grab_samples_out.append_array(_waiver_samples())
	elif sn.begins_with("items_") or sn.begins_with("use_") or sn.begins_with("dealer_"):
		_grab_samples_out.append_array(_item_samples())
	else:
		_grab_samples_out.append_array(_grab_samples())

	var last := 0
	for step in _scenario["timeline"]:
		last = max(last, int(step["at"]))
	if at >= last + int(_scenario["after"]):
		_write({"events": events, "samples": _grab_samples_out})
		return true
	return false

func _quiet_rng(node: Node) -> void:
	if "looping" in node:
		node.looping = false
	for c in node.get_children():
		_quiet_rng(c)

# items_*: tests/parity/parity_main.cpp ItemSamples is the twin; change both.
# Items by itemName, never by node name (@Node3D@N differs between engines).
func _item_samples() -> Array:
	var camxf: Camera3D = _root.get_node("Camera")
	var ct: Transform3D = camxf.global_transform
	var cb: Basis = ct.basis
	var im = _root.get_node("standalone managers/item manager")
	var out := [
		{"id": "f%d|camxf" % frame, "v": [
			cb.x.x, cb.x.y, cb.x.z, cb.y.x, cb.y.y, cb.y.z, cb.z.x, cb.z.y, cb.z.z,
			ct.origin.x, ct.origin.y, ct.origin.z, camxf.fov]},
		{"id": "f%d|items" % frame, "v": [
			1.0 if im.interaction_intake.interactionAllowed else 0.0,
			float(im.numberOfItemsGrabbed), float(im.numberOfOccupiedGrids),
			float(im.numberOfItemsGrabbed_enemy)]},
	]
	# What an item use changes: parity_main.cpp ItemSamples, same order.
	var rm = _root.get_node("standalone managers/round manager")
	var st := []
	for shell in rm.shellSpawner.sequenceArray:
		st.append(1.0 if shell == "live" else 0.0)
	st.append(-1.0)
	st.append_array([float(rm.health_player), float(rm.health_opponent), float(rm.currentShotgunDamage),
		1.0 if rm.dealerCuffed else 0.0, 1.0 if rm.playerCuffed else 0.0, 1.0 if rm.barrelSawedOff else 0.0])
	out.append({"id": "f%d|state" % frame, "v": st})
	out.append({"id": "f%d|dialogue" % frame,
		"s": String(_root.get_node("Camera/dialogue UI/main dialogue/dialogue text").text)})
	# The pointer ray and the branch it lands on: ItemSamples' ray/branch twin.
	var cam = _root.get_node("Camera")
	var im2 = _root.get_node("standalone managers/interaction manager")
	var cur = _root.get_node("standalone managers/cursor manager")
	var hit := ""
	if cam.result != null and not cam.result.is_empty() and cam.result.has("collider"):
		hit = String(_root.get_path_to(cam.result["collider"]))
	out.append({"id": "f%d|ray" % frame, "s": hit,
		"v": [cam.mouse.x, cam.mouse.y, 1.0 if cam.controller_overriding else 0.0]})
	var abr = im2.activeInteractionBranch
	out.append({"id": "f%d|branch" % frame, "s": (abr.interactionAlias if abr != null else ""),
		"v": [1.0 if cur.controller_active else 0.0, 1.0 if cur.cursor_visible else 0.0]})
	var k := 0
	for c in im.itemSpawnParent.get_children():
		if not (c is Node3D) or c.get_child_count() < 2:
			continue
		var br = c.get_child(1)
		if not ("itemName" in br) or String(br.itemName) == "":
			continue
		var gt: Transform3D = (c as Node3D).global_transform
		var o: Vector3 = gt.origin
		var b: Basis = gt.basis
		out.append({"id": "f%d|item%d" % [frame, k], "s": String(br.itemName), "v": [o.x, o.y, o.z,
			b.x.x, b.x.y, b.x.z, b.y.x, b.y.y, b.y.z, b.z.x, b.z.y, b.z.z]})
		k += 1
	return out

func _grab_samples() -> Array:
	var fo := get_root().gui_get_focus_owner()
	var cam = _root.get_node("Camera")
	var im = _root.get_node("standalone managers/interaction manager")
	var cur = _root.get_node("standalone managers/cursor manager")
	var hit := ""
	if cam.result != null and not cam.result.is_empty() and cam.result.has("collider"):
		hit = String(_root.get_path_to(cam.result["collider"]))
	var br = im.activeInteractionBranch
	var gun: Node3D = _root.get_node("tabletop parent/shotgun main parent/shotgun parent new")
	var td: Label3D = _root.get_node("ui parent_shooting decision/text dealer")
	var ty: Label3D = _root.get_node("ui parent_shooting decision/text you")
	var ind = _root.get_node("tabletop parent/shotgun main parent/shotgun parent new/pickup indicator branch")
	var shaker = _root.get_node("tabletop parent/shotgun main parent/shotgun parent new/obj shaker branch")
	var choice: Control = _root.get_node("Camera/dialogue UI/bracket ui/shooting choice")
	var anim: AnimationPlayer = _root.get_node("tabletop parent/shotgun main parent/anim_shotgun main parent")
	var desc: Label = _root.get_node("Camera/dialogue UI/item description/item name")
	var camxf: Camera3D = _root.get_node("Camera")
	var ct: Transform3D = camxf.global_transform
	var cb: Basis = ct.basis
	return [
		# The camera itself. The shot lerps it to "enemy" and back to "home", so
		# a shot scenario that does not sample it cannot tell the two apart.
		{"id": "f%d|camxf" % frame, "v": [
			cb.x.x, cb.x.y, cb.x.z, cb.y.x, cb.y.y, cb.y.z, cb.z.x, cb.z.y, cb.z.z,
			ct.origin.x, ct.origin.y, ct.origin.z, camxf.fov]},
		{"id": "f%d|focus" % frame, "s": (String(_root.get_path_to(fo)) if fo != null else "")},
		{"id": "f%d|ray" % frame, "s": hit,
			"v": [cam.mouse.x, cam.mouse.y, 1.0 if cam.controller_overriding else 0.0]},
		{"id": "f%d|branch" % frame, "s": (br.interactionAlias if br != null else ""),
			"v": [1.0 if cur.controller_active else 0.0, 1.0 if cur.cursor_visible else 0.0]},
		{"id": "f%d|gun" % frame, "v": [gun.transform.origin.x, gun.transform.origin.y, gun.transform.origin.z]},
		{"id": "f%d|choice" % frame, "v": [1.0 if choice.visible else 0.0,
			td.transparency, ty.transparency, td.modulate.r, ty.modulate.r,
			float(td.get_node("StaticBody3D").collision_layer),
			float(ty.get_node("StaticBody3D").collision_layer)]},
		{"id": "f%d|anim" % frame, "s": String(anim.current_animation)},
		{"id": "f%d|desc" % frame, "s": desc.text},
		# The two halves of the gun's x, separately: PickupIndicator lerps it and
		# ObjectShaker adds a random offset on top. Comparing only the sum cannot
		# say which of them differs.
		{"id": "f%d|slide" % frame, "v": [ind.current, ind.next, ind.elapsed,
			1.0 if ind.moving else 0.0]},
		{"id": "f%d|shake" % frame, "v": [shaker.originalPos.x,
			1.0 if shaker.shaking else 0.0]},
	]

func _write(body: Dictionary) -> void:
	body["scenario"] = _scenario["name"]
	var path := _abs(_ref_dir).path_join("%s.json" % _scenario["name"])
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		printerr("FAIL: cannot write ", path)
		quit(1); return
	f.store_string(JSON.stringify(body, "", false, true))
	f.close()
	print("wrote ", path)
	quit(0)

# menu_*: scenes/menu.tscn from frame 0. tests/parity/parity_main.cpp
# RunMenu + MenuSamples is the twin; change both.
#
# The menu's scripts read and write user:// (OptionsManager, SaveFileManager).
# The game's default user:// is %APPDATA%/Godot/app_userdata/Buckshot Roulette,
# which may hold a real install's saves, so a menu run refuses to start unless
# tools/godot/ref_game.sh has pointed user:// at a private directory (its
# override.cfg sets custom_user_dir_name = bsht_parity_user). There it starts
# from no saves at all, as the port's fresh save dir does.
const _MENU_USER_DIR := "bsht_parity_user"
const _MENU_SAVES := ["user://buckshotroulette_options_12.shell", "user://buckshotroulette_main_12.shell",
	"user://buckshotroulette_pills.shell", "user://buckshotroulette_playerstats.shell"]
var _menu_zero := -1
var _menu_samples_out: Array = []

# The private user:// (make_menu_references.sh), emptied of every save the
# scenes read. False (and quit) anywhere else: never touch a real install's.
func _private_user_dir() -> bool:
	if not OS.get_user_data_dir().ends_with(_MENU_USER_DIR):
		printerr("FAIL: this scenario runs only with user:// at ", _MENU_USER_DIR,
			" (use tools/godot/make_menu_references.sh); it is ", OS.get_user_data_dir())
		quit(2)
		return false
	for f in _MENU_SAVES:
		if FileAccess.file_exists(f):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(f))
	return true

func _begin_menu() -> void:
	if not _private_user_dir():
		return
	# Every menu scenario is windowed (it has frames); keep the desk cursor out.
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_MOUSE_PASSTHROUGH, true)
	seed(0)
	_root = (load("res://scenes/menu.tscn") as PackedScene).instantiate()
	get_root().add_child(_root)
	current_scene = _root

func _step_menu() -> bool:
	# change_scene_to_file swaps at the end of the frame that asked; this frame
	# sees it, so the request was frame - 1 (the port records the same frame).
	if not is_instance_valid(_root) or current_scene != _root:
		var cs := current_scene
		events.append("f%d:scene:%s" % [frame - 1, (cs.scene_file_path if cs != null else "")])
		_write({"events": events, "samples": _menu_samples_out})
		return true
	var start := int(_scenario["start_frame"])
	if frame < start:
		return false
	if frame == start:
		seed(int(_scenario["seed"]))
		if bool(_scenario.get("fog_off", false)):
			# Diagnostic (frames only): the scene without its Environment fog,
			# to tell a lighting difference from a fog one.
			for env in _root.find_children("*", "WorldEnvironment", true, false):
				env.environment.fog_enabled = false
		return false
	if _menu_zero < 0:
		var gate: Control = _root.get_node(NodePath(String(_scenario["wait_for_visible"])))
		if not gate.is_visible_in_tree():
			return false
		_menu_zero = frame
	var at := frame - _menu_zero
	for shot in _scenario.get("shots", []):
		if int(shot) == at - 1:
			var img := get_root().get_texture().get_image()
			img.save_png(_shot_path(_scenario["name"], at - 1))
	for step in _scenario.get("timeline", []):
		if int(step["at"]) == at and String(step["event"]) == "key":
			var k := InputEventKey.new()
			k.keycode = int(step["keycode"])
			k.pressed = bool(step["pressed"])
			k.device = 0
			Input.parse_input_event(k)
	_menu_samples_out.append_array(_menu_samples())
	var last := 0
	for step in _scenario.get("timeline", []):
		last = max(last, int(step["at"]))
	if at >= last + int(_scenario["after"]):
		_write({"events": events, "samples": _menu_samples_out})
		return true
	return false

func _menu_samples() -> Array:
	var mm = _root.get_node("standalone managers/menu manager")
	var om = _root.get_node("standalone managers/options manager")
	var fo := get_root().gui_get_focus_owner()
	var vis: Array = []
	for scr in mm.screens:
		vis.append(1.0 if scr.visible else 0.0)
	vis.append(1.0 if mm.title.visible else 0.0)
	vis.append(1.0 if mm.mouseblocker.visible else 0.0)
	return [
		{"id": "f%d|focus" % frame, "s": (String(_root.get_path_to(fo)) if fo != null else "")},
		{"id": "f%d|screen" % frame, "s": mm.currentScreen, "v": vis},
		{"id": "f%d|anim" % frame,
			"s": String(mm.animator_intro.current_animation) + "|" + String(mm.anim_creds.current_animation),
			"v": [1.0 if mm.waterfalls[0].is_playing() else 0.0]},
		{"id": "f%d|volume" % frame, "s": om.ui_volume.text,
			"v": [om.setting_volume, AudioServer.get_bus_volume_db(0),
				1.0 if FileAccess.file_exists(_MENU_SAVES[0]) else 0.0]},
		{"id": "f%d|cursor" % frame,
			"v": [1.0 if mm.cursor.controller_active else 0.0, 1.0 if mm.cursor.cursor_visible else 0.0]},
	]

# intro_*: main.tscn from frame 0, the restroom intro not skipped.
# tests/parity/parity_main.cpp RunIntro + IntroSamples is the twin; change both.
# "player": PlayerData fields set at start_frame (the revival's
# playerEnteringFromDeath is read 0.5 s after _ready, so frame 1 is in time).
var _intro_zero := -1
var _intro_samples_out: Array = []
var _intro_dealer_seen := false

func _begin_intro() -> void:
	if not _private_user_dir():
		return
	if bool(_scenario.get("pills", false)):
		# The Double or Nothing unlock (DonUnlockManager.UnlockMode): the pills
		# and the CRT appear in the restroom.
		var f := FileAccess.open("user://buckshotroulette_pills.shell", FileAccess.WRITE)
		f.store_var({"total_amount_selected": 0})
		f.close()
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_MOUSE_PASSTHROUGH, true)
	seed(0)
	_root = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	get_root().add_child(_root)
	current_scene = _root

func _step_intro() -> bool:
	var start := int(_scenario["start_frame"])
	if frame < start:
		return false
	var rm = _root.get_node("standalone managers/round manager")
	if frame == start:
		seed(int(_scenario["seed"]))
		var p: Dictionary = _scenario.get("player", {})
		if p.has("entering_from_death"):
			rm.playerData.playerEnteringFromDeath = bool(p["entering_from_death"])
		if p.has("signed"):
			rm.playerData.hasSignedWaiver = bool(p["signed"])
		if p.has("name"):
			rm.playerData.playername = String(p["name"])
		if p.has("batch"):
			rm.playerData.currentBatchIndex = int(p["batch"])
		return false
	if _intro_zero < 0:
		var gate_node = _root.get_node_or_null(NodePath(String(_scenario["wait_for_visible"])))
		if not (gate_node is Control):
			printerr("FAIL: wait_for_visible must name a Control: ", _scenario["wait_for_visible"])
			quit(2)
			return true
		var gate: Control = gate_node
		if not gate.is_visible_in_tree():
			return false
		_intro_zero = frame
	var at := frame - _intro_zero
	for shot in _scenario.get("shots", []):
		if int(shot) == at - 1:
			var img := get_root().get_texture().get_image()
			img.save_png(_shot_path(_scenario["name"], at - 1))
	for step in _scenario.get("timeline", []):
		if int(step["at"]) == at and String(step["event"]) == "key":
			var k := InputEventKey.new()
			k.keycode = int(step["keycode"])
			k.pressed = bool(step["pressed"])
			k.device = 0
			Input.parse_input_event(k)
	if int(_scenario.get("crt_at", -1)) == at:
		# The screen's interaction (InteractionManager "crt").
		_root.get_node("standalone managers/intro manager").Interaction_CRT()
	if String(_scenario["name"]).begins_with("intro_crt"):
		_intro_samples_out.append_array(_crt_samples())
	if not _intro_dealer_seen and String(rm.animator_dealer.current_animation) == "dealer return to table":
		_intro_dealer_seen = true
		events.append("f%d:dealer_return" % frame)
	_intro_samples_out.append_array(_intro_samples())
	var last := 0
	for step in _scenario.get("timeline", []):
		last = max(last, int(step["at"]))
	if at >= last + int(_scenario["after"]):
		_write({"events": events, "samples": _intro_samples_out})
		return true
	return false

func _intro_samples() -> Array:
	var im = _root.get_node("standalone managers/intro manager")
	var fo := get_root().gui_get_focus_owner()
	var cam: Camera3D = _root.get_node("Camera")
	var ct: Transform3D = cam.global_transform
	var cb: Basis = ct.basis
	var blocker: Control = _root.get_node("Camera/dialogue UI/viewblocker parent/viewblcoker")
	return [
		{"id": "f%d|camxf" % frame, "v": [
			cb.x.x, cb.x.y, cb.x.z, cb.y.x, cb.y.y, cb.y.z, cb.z.x, cb.z.y, cb.z.z,
			ct.origin.x, ct.origin.y, ct.origin.z, cam.fov]},
		{"id": "f%d|anim" % frame, "s": String(im.animator_camera.current_animation)},
		{"id": "f%d|flags" % frame, "v": [
			1.0 if im.intbranch_bathroomdoor.interactionAllowed else 0.0,
			1.0 if im.intbranch_backroomdoor.interactionAllowed else 0.0,
			1.0 if im.counting else 0.0,
			1.0 if blocker.visible else 0.0,
			1.0 if im.smokerdude_revival.visible else 0.0,
			1.0 if im.parent_pills.visible else 0.0]},
		{"id": "f%d|focus" % frame, "s": (String(_root.get_path_to(fo)) if fo != null else "")},
		{"id": "f%d|doors" % frame, "v": [
			_root.get_node("intro parent/bathroom door").rotation.x,
			_root.get_node("intro parent/bathroom door").rotation.y,
			_root.get_node("intro parent/bathroom door").rotation.z,
			_root.get_node("intro parent/backroom door").rotation.x,
			_root.get_node("intro parent/backroom door").rotation.y,
			_root.get_node("intro parent/backroom door").rotation.z]},
	]

# waiver_*: the grab path with the waiver unsigned. RunGrab + WaiverSamples is
# the twin. The release form is freed after Enter (Input_Enter:
# parent_waiver.queue_free()), so its branch is found by the path taken on the
# first sample, never held.
var _waiver_branch_path := NodePath()
func _waiver_samples() -> Array:
	var sm = _root.get_node("standalone managers/signature manager")
	if _waiver_branch_path.is_empty():
		_waiver_branch_path = _root.get_path_to(sm.intrbranch_waiver)
	var branch = _root.get_node_or_null(_waiver_branch_path)
	var rm = _root.get_node("standalone managers/round manager")
	var letters := ""
	for l in sm.letterArray:
		letters += l.text
	var signed := ""
	for l in sm.letterArray_signature_joined:
		signed += l.text
	return [
		{"id": "f%d|waiver" % frame, "s": letters + "|" + signed, "v": [
			float(sm.markerIndex),
			1.0 if rm.playerData.hasSignedWaiver else 0.0,
			1.0 if sm.marker.visible else 0.0,
			1.0 if (branch != null and branch.interactionAllowed) else 0.0,
			1.0 if sm.parent_signatureMachineMainParent.visible else 0.0,
			sm.marker.modulate.a]},
	]


# intro_crt_*: the restroom CRT boots, cycles and exits.
# tests/parity/parity_main.cpp CrtSamples is the twin.
func _crt_samples() -> Array:
	var crt = _root.get_node("standalone managers/crt manager")
	var icons := ""
	for icon in crt.iconbranches:
		icons += "1" if icon.instance.material_override.albedo_texture == icon.texture_active else "0"
	return [
		# The partition digits are random (the intro does not pin the global
		# RNG): only whether a counter shows its blank or spins is compared.
		{"id": "f%d|crt" % frame, "s": icons + "|" +
			("blank" if String(crt.array_partbranch[0].p.text).begins_with("  ") else "digits") + "|" +
			String(crt.anim_iconfade.current_animation), "v": [
			float(crt.window_index),
			1.0 if crt.viewing else 0.0,
			1.0 if crt.has_exited else 0.0,
			1.0 if crt.screenparent_stats.visible else 0.0,
			1.0 if crt.screenparent_leaderboard.visible else 0.0,
			1.0 if crt.array_bootup[0].visible else 0.0,
			1.0 if crt.array_bootuplogo[0].visible else 0.0,
			1.0 if crt.array_stats[0].visible else 0.0,
			1.0 if crt.branch_window.interactionAllowed else 0.0,
			1.0 if crt.exit.exitAllowed else 0.0,
			1.0 if crt.bathroom_broken.visible else 0.0]},
	]


# ending_*: main.tscn from the table, then the last batch cleared: EndMainBatch
# at index 2 rolls the briefcase machine in. Private user:// (ref_game.sh):
# the unlock and the lifetime stats are written.
# tests/parity/parity_main.cpp RunGrab + EndingSamples and app::ScenarioRunner
# (scene "ending") are the twins; change all three.
var _ending_zero := -1
var _ending_samples_out: Array = []

func _begin_ending() -> void:
	if not _private_user_dir():
		return
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_MOUSE_PASSTHROUGH, true)
	seed(0)
	_root = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	get_root().add_child(_root)
	current_scene = _root

func _step_ending() -> bool:
	# The unlock's change_scene_to_file swaps at the end of the frame that
	# asked; this frame sees it, so the request was frame - 1.
	if not is_instance_valid(_root) or current_scene != _root:
		var cs := current_scene
		events.append("f%d:scene:%s" % [frame - 1, (cs.scene_file_path if cs != null else "")])
		_write({"events": events, "samples": _ending_samples_out})
		return true
	var start := int(_scenario["start_frame"])
	if frame < start:
		return false
	var rm = _root.get_node("standalone managers/round manager")
	if frame == start:
		seed(int(_scenario["seed"]))
		rm.playerData.hasSignedWaiver = true
		var p: Dictionary = _scenario.get("player", {})
		rm.playerData.playername = String(p.get("name", ""))
		rm.playerData.stat_shotsFired = int(p.get("shots", 0))
		rm.playerData.stat_shellsEjected = int(p.get("shells", 0))
		rm.playerData.stat_doorsKicked = int(p.get("doors", 0))
		rm.playerData.stat_cigSmoked = int(p.get("cigs", 0))
		rm.playerData.stat_beerDrank = int(p.get("beer", 0))
		var cm = _root.get_node("standalone managers/camera manager")
		for s in cm.socketArray:
			if s.socketName == "home":
				cm.cam.transform.origin = s.pos
				cm.cam.rotation_degrees = s.rot
				cm.cam.fov = s.fov
		_root.get_node("standalone managers/intro manager").queue_free()
		_root.get_node("Camera/dialogue UI/viewblocker parent/viewblcoker").visible = false
		_root.get_node("light parent/lght_backroom main2").light_energy = 0.0
		if bool(_scenario.get("quiet_rng", false)):
			_quiet_rng(_root)
		if String(_scenario.get("ending", "win")) == "don":
			# The pills' SetupEndless, a fixed run time, batch 3 cleared:
			# EndMainBatch's endless branch starts BeginScoreLerp.
			_root.get_node("standalone managers/endless mode").SetupEndless()
			rm.counting = false
			rm.initial_time = float(_scenario.get("initial_time", 0.0))
		if String(_scenario.get("ending", "win")) == "true_death":
			# DeathManager.MainDeathRoutine's heaven branch.
			rm.wireIsCut_player = true
			rm.health_player = 0
			rm.OutOfHealth("player")
		else:
			rm.playerData.currentBatchIndex = 2
			rm.EndMainBatch()
		return false
	if _ending_zero < 0:
		var gate: Control = _root.get_node(NodePath(String(_scenario["wait_for_visible"])))
		if not gate.is_visible_in_tree():
			return false
		_ending_zero = frame
		print("briefcase reached at frame ", frame)
	var at := frame - _ending_zero
	for shot in _scenario.get("shots", []):
		if int(shot) == at - 1:
			var img := get_root().get_texture().get_image()
			img.save_png(_shot_path(_scenario["name"], at - 1))
	for step in _scenario.get("timeline", []):
		if int(step["at"]) == at and String(step["event"]) == "key":
			var k := InputEventKey.new()
			k.keycode = int(step["keycode"])
			k.pressed = bool(step["pressed"])
			k.device = 0
			Input.parse_input_event(k)
	_ending_samples_out.append_array(_ending_samples())
	if String(_scenario.get("ending", "win")) == "don":
		_ending_samples_out.append_array(_don_samples())
	var last := 0
	for step in _scenario.get("timeline", []):
		last = max(last, int(step["at"]))
	if at >= last + int(_scenario["after"]):
		_write({"events": events, "samples": _ending_samples_out})
		return true
	return false

func _ending_samples() -> Array:
	var b = _root.get_node("standalone managers/briefcase machine")
	var e = b.ending
	var cam: Camera3D = _root.get_node("Camera")
	var ct: Transform3D = cam.global_transform
	var cb: Basis = ct.basis
	var blocker: Control = _root.get_node("Camera/dialogue UI/viewblocker parent/viewblcoker")
	var fo := get_root().gui_get_focus_owner()
	var texts: String = e.label_congrats.text
	for l in e.label_array:
		texts += "|" + l.text
	texts += "|" + e.unlocker.ui.text
	return [
		{"id": "f%d|camxf" % frame, "v": [
			cb.x.x, cb.x.y, cb.x.z, cb.y.x, cb.y.y, cb.y.z, cb.z.x, cb.z.y, cb.z.z,
			ct.origin.x, ct.origin.y, ct.origin.z, cam.fov]},
		{"id": "f%d|anim" % frame, "s": String(b.anim_machine.current_animation) + "|" +
			String(b.anim_lid.current_animation) + "|" + String(e.animator_pan.current_animation) + "|" +
			String(e.animator_cam.current_animation) + "|" + String(e.animator_viewblocker.current_animation) + "|" +
			String(e.animator_anykey.current_animation)},
		{"id": "f%d|flags" % frame, "v": [
			1.0 if b.parent_eyes.visible else 0.0,
			1.0 if b.parent_machine.visible else 0.0,
			1.0 if b.intbranch_L.interactionAllowed else 0.0,
			1.0 if b.intbranch_R.interactionAllowed else 0.0,
			1.0 if b.intbranch_lid.interactionAllowed else 0.0,
			1.0 if b.btnParent_briefcase.visible else 0.0,
			1.0 if blocker.visible else 0.0,
			1.0 if e.waitingForInput else 0.0,
			1.0 if e.blockout[0].visible else 0.0,
			1.0 if FileAccess.file_exists("user://buckshotroulette_pills.shell") else 0.0,
			1.0 if FileAccess.file_exists("user://buckshotroulette_playerstats.shell") else 0.0]},
		{"id": "f%d|text" % frame, "s": texts, "v": [
			float(e.label_congrats.visible_characters),
			float(e.unlocker.ui.visible_characters),
			1.0 if e.label_congrats.visible else 0.0,
			1.0 if e.label_array[5].visible else 0.0,
			e.pp.environment.adjustment_brightness,
			e.pp.environment.adjustment_contrast,
			e.pp.environment.adjustment_saturation]},
		{"id": "f%d|focus" % frame, "s": (String(_root.get_path_to(fo)) if fo != null else "")},
		{"id": "f%d|player" % frame, "v": [
			1.0 if _root.get_node("standalone managers/round manager").playerData.playerEnteringFromDeath else 0.0,
			1.0 if _root.get_node("standalone managers/round manager").playerData.enteringFromTrueDeath else 0.0]},
	]


# "ending": "don": the score lerp, the yes/no machine, the restart.
# tests/parity/parity_main.cpp DonSamples is the twin.
func _don_samples() -> Array:
	var rm = _root.get_node("standalone managers/round manager")
	return [
		{"id": "f%d|don" % frame, "s": String(rm.ui_score.text) + "|" + String(rm.anim_doubleor.current_animation) +
			"|" + String(rm.anim_yes.current_animation) + "|" + String(rm.anim_no.current_animation) + "|" +
			String(rm.camera.activeSocket), "v": [
			1.0 if rm.ui_score.visible else 0.0,
			1.0 if rm.ui_doubleornothing.visible else 0.0,
			1.0 if rm.intbranch_yes.interactionAllowed else 0.0,
			1.0 if rm.intbranch_no.interactionAllowed else 0.0,
			1.0 if rm.btnParent_doubleor.visible else 0.0,
			float(rm.playerData.currentBatchIndex),
			1.0 if rm.endless else 0.0,
			1.0 if rm.doubling else 0.0,
			float(rm.endscore if rm.endscore != null else 0),
			float(rm.double_or_nothing_rounds_beat),
			float(rm.mainBatchIndex)]},
	]


# heaven_*: scenes/heaven.tscn from frame 0. HeavenManager runs itself from
# _ready; the scenario only seeds and presses keys.
# tests/parity/parity_main.cpp RunHeaven + HeavenSamples is the twin.
var _heaven_zero := -1
var _heaven_samples_out: Array = []

func _begin_heaven() -> void:
	if not _private_user_dir():
		return
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_MOUSE_PASSTHROUGH, true)
	seed(0)
	_root = (load("res://scenes/heaven.tscn") as PackedScene).instantiate()
	get_root().add_child(_root)
	current_scene = _root

func _step_heaven() -> bool:
	if not is_instance_valid(_root) or current_scene != _root:
		var cs := current_scene
		events.append("f%d:scene:%s" % [frame - 1, (cs.scene_file_path if cs != null else "")])
		_write({"events": events, "samples": _heaven_samples_out})
		return true
	var start := int(_scenario["start_frame"])
	if frame < start:
		return false
	if frame == start:
		seed(int(_scenario["seed"]))
		return false
	if _heaven_zero < 0:
		var gate: Control = _root.get_node(NodePath(String(_scenario["wait_for_visible"])))
		if not gate.is_visible_in_tree():
			return false
		_heaven_zero = frame
	var at := frame - _heaven_zero
	for shot in _scenario.get("shots", []):
		if int(shot) == at - 1:
			var img := get_root().get_texture().get_image()
			img.save_png(_shot_path(_scenario["name"], at - 1))
	for step in _scenario.get("timeline", []):
		if int(step["at"]) == at and String(step["event"]) == "key":
			var k := InputEventKey.new()
			k.keycode = int(step["keycode"])
			k.pressed = bool(step["pressed"])
			k.device = 0
			Input.parse_input_event(k)
	_heaven_samples_out.append_array(_heaven_samples())
	var last := 0
	for step in _scenario.get("timeline", []):
		last = max(last, int(step["at"]))
	if at >= last + int(_scenario["after"]):
		_write({"events": events, "samples": _heaven_samples_out})
		return true
	return false

func _heaven_samples() -> Array:
	var h = _root.get_node("standalone managers/heaven manager")
	var im = _root.get_node("standalone managers/interaction manager")
	var cam: Camera3D = _root.get_node("Camera")
	var ct: Transform3D = cam.global_transform
	var cb: Basis = ct.basis
	var fo := get_root().gui_get_focus_owner()
	return [
		{"id": "f%d|camxf" % frame, "v": [
			cb.x.x, cb.x.y, cb.x.z, cb.y.x, cb.y.y, cb.y.z, cb.z.x, cb.z.y, cb.z.z,
			ct.origin.x, ct.origin.y, ct.origin.z, cam.fov]},
		{"id": "f%d|anim" % frame, "s": String(h.animator.current_animation)},
		{"id": "f%d|flags" % frame, "v": [
			1.0 if h.viewblocker.visible else 0.0,
			1.0 if h.btn_door.visible else 0.0,
			1.0 if h.intbranch_heavendoor.interactionAllowed else 0.0,
			1.0 if h.buttonArray[0].isActive else 0.0,
			1.0 if h.buttonArray[1].isActive else 0.0,
			1.0 if im.checking else 0.0,
			1.0 if h.cursor.cursor_visible else 0.0,
			1.0 if h.cursor.controller_active else 0.0]},
		{"id": "f%d|focus" % frame, "s": (String(_root.get_path_to(fo)) if fo != null else "")},
	]
