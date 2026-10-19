# Export the main.tscn scene graph: node hierarchy, transforms, and the 29
# OmniLight3D transforms that the offline light baker will consume.
#
# A SceneTree script, not an EditorScript: an EditorScript needs the editor
# GUI, while a SceneTree script runs headless, so it can be automated.
#
# Run:
#   godot --headless --path <proj> --script res://tools/godot/export_scene_graph.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")

var OUT_DIR := SceneArgs.out_root()

# Frames to let visibility and transforms propagate before measuring.
const SETTLE_FRAMES := 3

var _root: Node = null
var _frames := 0

func _initialize() -> void:
	var abs_dir := ProjectSettings.globalize_path(OUT_DIR)
	DirAccess.make_dir_recursive_absolute(abs_dir)

	print("loading main.tscn (137 MB, this takes a moment)...")
	var scene: PackedScene = load(SceneArgs.scene_path())
	if scene == null:
		printerr("FAIL: could not load main.tscn")
		quit(1); return

	var root := scene.instantiate()
	if root == null:
		printerr("FAIL: could not instantiate main.tscn")
		quit(1); return

	# The root must be in the tree, and the tree must have ticked.
	#
	# is_visible_in_tree() on an orphan node just mirrors `visible`, so an
	# instantiated scene never added to the tree reports a hidden parent's
	# children as visible (the children of `item grid indicator parent` would
	# draw as solid white quads across the table). Adding the root and letting a
	# few frames pass makes visibility propagate, the same reason export_ui.gd
	# counts frames before measuring Control rects.
	get_root().add_child(root)
	_root = root
	return

func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_export(_root)
	quit()
	return true

func _export(root: Node) -> void:
	var abs_dir := ProjectSettings.globalize_path(OUT_DIR)
	var nodes: Array = []
	var lights: Array = []
	var counts: Dictionary = {}
	_walk(root, root, nodes, lights, counts)

	var data := {
		"nodes": nodes,
		"lights": lights,
		"counts": counts,
	}
	var out_abs := abs_dir + "/scene_graph" + SceneArgs.suffix() + ".json"
	var f := FileAccess.open(out_abs, FileAccess.WRITE)
	if f == null:
		printerr("FAIL: cannot write ", out_abs)
		quit(1); return
	f.store_string(JSON.stringify(data, "  "))
	f.close()

	print("nodes=", nodes.size(), " lights=", lights.size())
	print("MeshInstance3D=", counts.get("MeshInstance3D", 0),
		  " OmniLight3D=", counts.get("OmniLight3D", 0),
		  " Skeleton3D=", counts.get("Skeleton3D", 0),
		  " AnimationPlayer=", counts.get("AnimationPlayer", 0))
	print("wrote ", out_abs)
	root.free()
	quit(0)

func _walk(n: Node, root: Node, nodes: Array, lights: Array, counts: Dictionary) -> void:
	var t := n.get_class()
	counts[t] = int(counts.get(t, 0)) + 1
	counts["total"] = int(counts.get("total", 0)) + 1

	var entry := {
		"path": String(root.get_path_to(n)),
		"type": t,
		"name": String(n.name),
	}
	if n is Node3D:
		entry["transform"] = _xform(n.transform)
		entry["visible"] = n.visible

		# A node's OWN visible flag stays true under a hidden parent, so it is
		# not what "on screen" means. The item grid indicators are the case that
		# caught this: their own flag is set, their parent is hidden, and the
		# baker drew them as solid white quads lying across the table.
		entry["visible_in_tree"] = n.is_visible_in_tree()
	if n is GeometryInstance3D:
		# GeometryInstance3D.transparency, 0 opaque .. 1 fully transparent.
		#
		# ButtonClass_Main.gd drives this for the six controls that highlight
		# geometry instead of a 2D bracket (`is3D`): _ready() calls SetUI(false)
		# on a dynamic button, which sets ui_3D.transparency = 1, and focusing it
		# sets 0. So the five `bracket selection 3d_*` meshes are invisible at
		# rest (drawn opaque, one is a blank quad across the waiver).
		#
		# This holds only because the exporter adds the root to the SceneTree and
		# lets frames pass, so _ready() has run. On an orphan node it would read
		# the authored default.
		entry["transparency"] = n.transparency
	if n is MeshInstance3D and n.mesh != null:
		var rn: String = n.mesh.resource_name
		entry["mesh_id"] = rn if rn != "" else str(n.mesh.get_rid())
		entry["surfaces"] = n.mesh.get_surface_count()
		entry["skinned"] = n.skin != null
	nodes.append(entry)

	if n is OmniLight3D:
		lights.append({
			"path": String(root.get_path_to(n)),
			"color": [n.light_color.r, n.light_color.g, n.light_color.b],
			"energy": n.light_energy,
			"range": n.omni_range,
			# The decay exponent. Godot's omni falloff is
			#   nd = (d * inv_range)^4 ; nd = max(1 - nd, 0) ; nd = nd^2
			#   result = nd * pow(max(d, 0.0001), -attenuation)
			# Without this the bake has only the geometric term, the falloff is
			# far too generous, and 29 summed lights drive the room past white.
			"attenuation": n.omni_attenuation,
			"transform": _xform(n.transform),
		})

	for c in n.get_children():
		_walk(c, root, nodes, lights, counts)

func _xform(t: Transform3D) -> Array:
	var b := t.basis
	return [
		b.x.x, b.x.y, b.x.z, 0.0,
		b.y.x, b.y.y, b.y.z, 0.0,
		b.z.x, b.z.y, b.z.z, 0.0,
		t.origin.x, t.origin.y, t.origin.z, 1.0,
	]
