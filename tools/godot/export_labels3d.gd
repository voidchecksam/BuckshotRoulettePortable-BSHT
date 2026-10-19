# Every Label3D declared in main.tscn: what it says and how it draws, plus its
# world transform at load (from scene_graph.json) and the text Godot shows
# after _ready (from ui_fonts.json). Writes labels3d.json.
#
# Only labels main.tscn declares itself count, not ones inside instanced
# scenes. The instance is never added to the tree, so _ready has not run and
# every value is the authored one.
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")

func _init() -> void:
	var root: Node = load("res://scenes/main.tscn").instantiate()
	var labels: Array = []
	_collect(root, root, labels)
	_add_world_transforms(labels)
	_adopt_runtime_text(labels)
	root.free()
	if labels.is_empty():
		printerr("FATAL: no Label3D nodes found")
		quit(1)
		return
	var f := FileAccess.open(SceneArgs.extracted("labels3d.json"), FileAccess.WRITE)
	if f == null:
		printerr("FATAL: cannot write ", SceneArgs.extracted("labels3d.json"))
		quit(1)
		return
	f.store_string(JSON.stringify({"count": labels.size(), "labels": labels}, " ", false))
	print("wrote labels3d.json: ", labels.size(), " Label3D")
	quit(0)

func _collect(n: Node, root: Node, labels: Array) -> void:
	if n is Label3D and n.owner == root:
		labels.append(_entry(n, String(root.get_path_to(n))))
	for c in n.get_children():
		_collect(c, root, labels)

func _entry(l: Label3D, path: String) -> Dictionary:
	return {
		"path": path,
		"text": l.text,
		"font": l.font.resource_path if l.font else "",
		"font_size": l.font_size,
		"pixel_size": l.pixel_size,
		"modulate": [l.modulate.r, l.modulate.g, l.modulate.b, l.modulate.a],
		"outline_modulate": [l.outline_modulate.r, l.outline_modulate.g, l.outline_modulate.b, l.outline_modulate.a],
		"outline_size": l.outline_size,
		"horizontal_alignment": l.horizontal_alignment,
		"vertical_alignment": l.vertical_alignment,
		"line_spacing": l.line_spacing,
		"width": l.width,
		"autowrap_mode": l.autowrap_mode,
		"billboard": l.billboard,
		"visible": l.visible,
		"transparency": l.transparency,
		"uppercase": l.uppercase,
		"no_depth_test": l.no_depth_test,
		"shaded": l.shaded,
		"double_sided": l.double_sided,
	}

func _read_json(name: String) -> Variant:
	var path := SceneArgs.extracted(name)
	if not FileAccess.file_exists(path):
		printerr("WARN ", name, " missing")
		return null
	return JSON.parse_string(FileAccess.get_file_as_string(path))

func _add_world_transforms(labels: Array) -> void:
	var graph = _read_json("scene_graph.json")
	if graph == null:
		return
	var nodes: Array = graph["nodes"] if graph is Dictionary else graph
	var local := {}
	var in_tree := {}
	var own := {}
	for n in nodes:
		if n.has("path"):
			local[n["path"]] = n.get("transform")
			in_tree[n["path"]] = n.get("visible_in_tree")
			own[n["path"]] = n.get("visible")
	for label in labels:
		var parts: PackedStringArray = label["path"].split("/")
		if in_tree.get(label["path"]) != null:
			label["visible_in_tree"] = bool(in_tree[label["path"]])
		# Ancestors hidden at load: scripts show them later, so the runtime
		# needs to know which one a label is waiting on.
		var hidden: Array = []
		for i in range(1, parts.size()):
			var p := "/".join(parts.slice(0, i))
			if own.get(p) == false:
				hidden.append(p)
		label["hidden_ancestors"] = hidden
		var world := [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]
		var found := true
		for i in range(1, parts.size() + 1):
			var t = local.get("/".join(parts.slice(0, i)))
			if t == null:
				if i == parts.size():
					found = false
				continue
			if t.size() == 16:
				world = _mul(world, t)
		if found:
			label["world"] = world

# Column-major 4x4, m[col * 4 + row], as scene_graph.json stores them.
func _mul(a: Array, b: Array) -> Array:
	var r := []
	r.resize(16)
	for c in 4:
		for row in 4:
			var s := 0.0
			for k in 4:
				s += a[k * 4 + row] * b[c * 4 + k]
			r[c * 4 + row] = s
	return r

# LeaderboardManager and SignatureManager clear their labels in _ready, so the
# authored strings never reach the screen. `text` becomes what Godot shows
# (export_fonts.gd records it) and `authored_text` keeps the scene's.
#
# bsht-extract runs this twice: before export_fonts.gd exists, only for the
# authored texts export_dialogue.gd reads, and again after it.
func _adopt_runtime_text(labels: Array) -> void:
	for label in labels:
		label["authored_text"] = label["text"]
	var fonts = _read_json("ui_fonts.json")
	if fonts == null:
		return
	var runtime := {}
	for e in fonts.get("label3d_text", []):
		runtime[e["path"]] = e["text"]
	for label in labels:
		if runtime.has(label["path"]):
			label["text"] = runtime[label["path"]]
