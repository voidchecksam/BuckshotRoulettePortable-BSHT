# Export the dialogue that lives in the scene, not in the scripts.
#
# ShellLoader declares `@export_multiline var introductionDialogues :
# Array[String]` and then plays nine of them, one every three seconds, the
# first time the player sits down. The script holds the timing and the order;
# the words are authored per instance in main.tscn. They are not tr() keys
# either, so the translation tables do not have them.
#
# Walks every scripted node and dumps its exported String and Array[String]
# properties, so nothing authored in the scene is missed.
#
# Run:
#   godot --headless --path <proj> --script res://tools/godot/export_scene_strings.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")
var OUT_PATH := SceneArgs.extracted("scene_strings.json")
const SETTLE_FRAMES := 2

var _root: Node = null
var _frames := 0

func _initialize() -> void:
	print("loading main.tscn (137 MB, this takes a moment)...")
	var scene: PackedScene = load("res://scenes/main.tscn")
	if scene == null:
		print("FATAL: could not load main.tscn"); quit(1); return
	_root = scene.instantiate()
	if _root == null:
		print("FATAL: could not instantiate main.tscn"); quit(1); return
	get_root().add_child(_root)

func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_export()
	quit()
	return true

func _export() -> void:
	var out := {}
	var nodes := 0
	var strings := 0
	_walk(_root, _root, out, nodes, strings)

	# Recount rather than thread counters through the recursion.
	nodes = out.size()
	strings = 0
	for path in out:
		for prop in out[path]:
			var v = out[path][prop]
			strings += v.size() if v is Array else 1

	var f := FileAccess.open(OUT_PATH, FileAccess.WRITE)
	if f == null:
		print("FATAL: cannot write ", OUT_PATH); quit(1); return
	f.store_string(JSON.stringify({"nodes": out}, "  "))
	f.close()

	print("scripted nodes with authored text: ", nodes, ", strings: ", strings)
	print("wrote ", OUT_PATH)

func _walk(n: Node, root: Node, out: Dictionary, nodes: int, strings: int) -> void:
	if n.get_script() != null:
		var found := {}
		for p in n.get_property_list():
			# Exported properties only. PROPERTY_USAGE_SCRIPT_VARIABLE marks
			# script vars; the storage flag separates exports from plain ones.
			if (p["usage"] & PROPERTY_USAGE_STORAGE) == 0:
				continue
			if (p["usage"] & PROPERTY_USAGE_SCRIPT_VARIABLE) == 0:
				continue

			var value = n.get(p["name"])
			if value is String:
				# Ignore short scalars: aliases, node names and flags are not
				# dialogue and would bury the lines that are.
				if (value as String).length() >= 8:
					found[p["name"]] = value
			elif value is Array:
				var texts: Array = []
				for e in value:
					if e is String and (e as String).length() > 0:
						texts.append(e)
				if texts.size() > 0:
					found[p["name"]] = texts
			elif value is PackedStringArray:
				var packed: Array = []
				for e in value:
					packed.append(e)
				if packed.size() > 0:
					found[p["name"]] = packed

		if found.size() > 0:
			out[String(root.get_path_to(n))] = found

	for c in n.get_children():
		_walk(c, root, out, nodes, strings)
