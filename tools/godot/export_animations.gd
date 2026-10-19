# Export the animation data: every track with its keyframes.
#
# Most tracks are TYPE_VALUE, so this is mostly a property animation system:
# the port needs to set named properties over time far more than it needs
# skinning.
#
# Values are written by kind so the C++ side never has to guess how to
# interpret a keyframe:
#   "f"    float
#   "b"    bool
#   "i"    int
#   "v2"   [x, y]
#   "v3"   [x, y, z]
#   "q"    [x, y, z, w]     quaternion
#   "c"    [r, g, b, a]
#   "s"    string
#   "nil"  null
#   "?"    anything else, with its Godot type id, so the gap is visible rather
#          than silently dropped
#
# Run:
#   godot --headless --path <proj> --script res://tools/godot/export_animations.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")

var OUT_PATH := SceneArgs.extracted("animations.json")

func _initialize() -> void:
	var scene: PackedScene = load(SceneArgs.scene_path())
	if scene == null:
		print("FATAL: could not load main.tscn")
		quit(1); return
	var root: Node = scene.instantiate()
	if root == null:
		print("FATAL: could not instantiate main.tscn")
		quit(1); return
	get_root().add_child(root)

	var players: Array = []
	_find_players(root, root, players)

	var out_players: Array = []
	var total_anims := 0
	var total_tracks := 0
	var total_keys := 0
	var unknown_kinds := {}

	for p in players:
		var player: AnimationPlayer = p["node"]
		var anims: Array = []
		for anim_name in player.get_animation_list():
			var anim: Animation = player.get_animation(anim_name)
			if anim == null:
				continue
			var tracks: Array = []
			for t in range(anim.get_track_count()):
				var track := _export_track(anim, t, unknown_kinds)
				total_keys += int(track["keys"].size())
				tracks.append(track)
			total_tracks += tracks.size()
			total_anims += 1
			anims.append({
				"name": String(anim_name),
				"length": anim.length,
				"loop": int(anim.loop_mode),
				"step": anim.step,
				"tracks": tracks,
			})
		# Track paths resolve against the player's root node, which defaults to the
		# parent but need not be: one player in this scene sets it a level higher.
		# Export it rather than guess.
		var root_abs := ""
		var rn: Node = player.get_node_or_null(player.root_node)
		if rn != null:
			root_abs = String(root.get_path_to(rn))
		out_players.append({
			"path": p["path"],
			"root": root_abs,
			"autoplay": String(player.autoplay),
			"animations": anims,
		})

	var data := {
		# 2: tracks carry update mode and loop wrap, keys carry transition.
		"format": 2,
		"players": out_players,
		"totals": {
			"players": out_players.size(),
			"animations": total_anims,
			"tracks": total_tracks,
			"keys": total_keys,
		},
		"unknown_value_kinds": unknown_kinds,
	}

	var f := FileAccess.open(SceneArgs.out(OUT_PATH), FileAccess.WRITE)
	if f == null:
		print("FATAL: cannot write ", SceneArgs.out(OUT_PATH))
		quit(1); return
	f.store_string(JSON.stringify(data))
	f.close()

	print("players=", out_players.size(), " animations=", total_anims,
		  " tracks=", total_tracks, " keys=", total_keys)
	if unknown_kinds.size() > 0:
		# Never silent: an unhandled value type is a track that will not play,
		# and finding that out from a still frame on the device is expensive.
		print("WARNING unhandled value types: ", unknown_kinds)
	print("wrote ", ProjectSettings.globalize_path(SceneArgs.out(OUT_PATH)))
	root.free()
	quit(0)

func _export_track(anim: Animation, t: int, unknown: Dictionary) -> Dictionary:
	var type_id := int(anim.track_get_type(t))
	var track := {
		"type": type_id,
		"path": String(anim.track_get_path(t)),
		"interp": int(anim.track_get_interpolation_type(t)),
		# Godot samples with these too: a DISCRETE value track uses nearest and
		# sets only the keys the playhead crosses; loop_wrap decides whether the
		# last key blends into the first on a looping animation.
		"loop_wrap": anim.track_get_interpolation_loop_wrap(t),
		"keys": [],
	}
	if type_id == Animation.TYPE_VALUE:
		track["update"] = int(anim.value_track_get_update_mode(t))

	for k in range(anim.track_get_key_count(t)):
		var time: float = anim.track_get_key_time(t, k)
		# The key's easing. Godot eases the blend factor by it, and 0 holds the key.
		var entry := {"t": time, "tr": anim.track_get_key_transition(t, k)}

		match type_id:
			Animation.TYPE_METHOD:
				entry["m"] = String(anim.method_track_get_name(t, k))
				entry["a"] = _export_args(anim.method_track_get_params(t, k), unknown)
			Animation.TYPE_AUDIO:
				var stream = anim.audio_track_get_key_stream(t, k)
				entry["stream"] = "" if stream == null else String(stream.resource_path)
				entry["start"] = anim.audio_track_get_key_start_offset(t, k)
				entry["end"] = anim.audio_track_get_key_end_offset(t, k)
			Animation.TYPE_BEZIER:
				entry["v"] = anim.bezier_track_get_key_value(t, k)
			_:
				var v = anim.track_get_key_value(t, k)
				var tagged := _tag_value(v, unknown)
				entry["k"] = tagged["k"]
				entry["v"] = tagged["v"]

		track["keys"].append(entry)

	return track

func _export_args(args: Array, unknown: Dictionary) -> Array:
	var out: Array = []
	for a in args:
		out.append(_tag_value(a, unknown))
	return out

# Tags a value with its kind so the C++ reader never guesses.
func _tag_value(v, unknown: Dictionary) -> Dictionary:
	var t := typeof(v)
	match t:
		TYPE_NIL:     return {"k": "nil", "v": null}
		TYPE_BOOL:    return {"k": "b", "v": bool(v)}
		TYPE_INT:     return {"k": "i", "v": int(v)}
		TYPE_FLOAT:   return {"k": "f", "v": float(v)}
		TYPE_STRING, TYPE_STRING_NAME, TYPE_NODE_PATH:
			return {"k": "s", "v": String(v)}
		TYPE_VECTOR2: return {"k": "v2", "v": [v.x, v.y]}
		TYPE_VECTOR3: return {"k": "v3", "v": [v.x, v.y, v.z]}
		TYPE_QUATERNION: return {"k": "q", "v": [v.x, v.y, v.z, v.w]}
		TYPE_COLOR:   return {"k": "c", "v": [v.r, v.g, v.b, v.a]}
	# Anything else is recorded WITH its type id rather than dropped, so the
	# gap shows up in the export summary instead of as a track that never plays.
	var key := "type_" + str(t)
	unknown[key] = int(unknown.get(key, 0)) + 1
	return {"k": "?", "v": str(v), "godot_type": t}

func _find_players(n: Node, root: Node, out: Array) -> void:
	if n is AnimationPlayer:
		out.append({"node": n, "path": String(root.get_path_to(n))})
	for c in n.get_children():
		_find_players(c, root, out)
