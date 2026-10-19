# The model library the bake reads: every mesh the game's scenes draw, written
# as .glb files grouped by the model it was imported from, plus
# models/_mesh_names.json (mesh name -> file and mesh index) and
# models/_manifest.json (counts).
#
# The meshes come from the scenes, not from .godot/imported: the scenes carry
# their own copies, which are what the game draws, and v2.2's pack holds its
# imported model scenes in Godot 4.3's format, which 4.1 cannot read.
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")
const IMPORTED_DIR := "res://.godot/imported"
const SCENES := ["res://scenes/main.tscn", "res://scenes/menu.tscn", "res://scenes/heaven.tscn",
	"res://instances/shell.tscn"]
const LOOSE_MODEL := "scene meshes"   # meshes whose model name is unknown

func _initialize() -> void:
	var out_dir := SceneArgs.extracted("models")
	var out_abs := ProjectSettings.globalize_path(out_dir)
	DirAccess.make_dir_recursive_absolute(out_abs)

	var meshes := {}   # resource_name -> Mesh
	for path in SCENES + _item_scenes():
		var packed: PackedScene = load(path)
		if packed == null:
			printerr("FATAL: cannot load ", path)
			quit(1)
			return
		var scene_root := packed.instantiate()
		var clash := _collect(scene_root, meshes)
		scene_root.free()
		if clash != "":
			printerr("FATAL: two scenes carry different meshes named ", clash)
			quit(1)
			return

	var known := _model_names()
	var groups := {}   # model -> mesh names
	for mesh_name in meshes:
		var model := _model_of(mesh_name, known)
		if not groups.has(model):
			groups[model] = []
		groups[model].append(mesh_name)

	var index: Array = []
	var manifest: Array = []
	var total_tris := 0
	var models: Array = groups.keys()
	models.sort()
	for model in models:
		var names: Array = groups[model]
		names.sort()
		var entry := _write_model(String(model), names, meshes, out_abs)
		if entry.is_empty():
			quit(1)
			return
		index.append(entry["index"])
		manifest.append(entry["manifest"])
		total_tris += int(entry["manifest"]["triangles"])

	_write_json(out_dir.path_join("_mesh_names.json"), {
		"models": index,
		"totals": {"models": index.size(), "named_meshes": meshes.size(), "unresolved_nodes": 0},
	})
	_write_json(out_dir.path_join("_manifest.json"), {
		"models": manifest,
		"totals": {"models": manifest.size(), "meshes": meshes.size(), "triangles": total_tris},
		"failed": [],
		"skipped": [],
	})
	print("models=", index.size(), " meshes=", meshes.size(), " tris=", total_tris)
	quit(0)

# One .glb per model. Nodes are named m0, m1, ... so the index does not depend
# on which characters a node name may hold; the name list maps them back.
func _write_model(model: String, names: Array, meshes: Dictionary, out_abs: String) -> Dictionary:
	var holder := Node3D.new()
	holder.name = "model"
	var tris := 0
	var skinned := false
	for i in names.size():
		var mi := MeshInstance3D.new()
		mi.name = "m%d" % i
		mi.mesh = meshes[names[i]]
		holder.add_child(mi)
		tris += _triangles(mi.mesh)
		skinned = skinned or _has_bones(mi.mesh)
	root.add_child(holder)
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var glb := model.replace(" ", "_") + ".glb"
	var ok := doc.append_from_scene(holder, state) == OK and \
		doc.write_to_filesystem(state, out_abs.path_join(glb)) == OK
	var mesh_names: Array = []
	mesh_names.resize(state.get_meshes().size())
	mesh_names.fill("")
	for gn in state.get_nodes():
		var node_name := String(gn.resource_name)
		if gn.mesh >= 0 and gn.mesh < mesh_names.size() and node_name.begins_with("m"):
			mesh_names[gn.mesh] = names[int(node_name.substr(1))]
	root.remove_child(holder)
	holder.free()
	if not ok:
		printerr("FATAL: cannot write ", glb)
		return {}
	return {
		"index": {"glb": glb, "meshes": mesh_names, "unresolved": []},
		"manifest": {"glb": glb, "meshes": names.size(), "triangles": tris, "has_skin": skinned, "animations": 0},
	}

# Adds every named mesh under `n`. Returns the name of a mesh that differs from
# one already collected under the same name, or "".
func _collect(n: Node, meshes: Dictionary) -> String:
	if n is MeshInstance3D and n.mesh != null and n.mesh.resource_name != "":
		var m: Mesh = n.mesh
		if not meshes.has(m.resource_name):
			meshes[m.resource_name] = m
		elif _shape(meshes[m.resource_name]) != _shape(m):
			return m.resource_name
	for c in n.get_children():
		var clash := _collect(c, meshes)
		if clash != "":
			return clash
	return ""

# Surface and vertex counts: enough to tell two different meshes apart.
func _shape(m: Mesh) -> Array:
	var out := []
	for si in m.get_surface_count():
		out.append(m.surface_get_arrays(si)[Mesh.ARRAY_VERTEX].size())
	return out

# The models the game imported, from .godot/imported's file names
# ("name.glb-<md5>.scn"). Only the names are read, never the files.
func _model_names() -> Array:
	var out: Array = []
	for f in DirAccess.get_files_at(IMPORTED_DIR):
		var at := String(f).find(".glb-")
		if at > 0 and not out.has(String(f).substr(0, at)):
			out.append(String(f).substr(0, at))
	return out

# A mesh named "<model>_<mesh>" belongs to the longest model name it starts with.
func _model_of(mesh_name: String, known: Array) -> String:
	var best := ""
	for m in known:
		if mesh_name.begins_with(String(m) + "_") and String(m).length() > best.length():
			best = String(m)
	return best if best != "" else LOOSE_MODEL

func _item_scenes() -> Array:
	var out: Array = []
	for f in DirAccess.get_files_at("res://instances"):
		var name := String(f).trim_suffix(".remap")
		if name.begins_with("item_") and name.ends_with(".tscn") and not out.has("res://instances/" + name):
			out.append("res://instances/" + name)
	out.sort()
	return out

func _triangles(m: Mesh) -> int:
	var tris := 0
	for si in m.get_surface_count():
		var arrays: Array = m.surface_get_arrays(si)
		var idx = arrays[Mesh.ARRAY_INDEX]
		tris += int(idx.size() / 3) if idx != null else int(arrays[Mesh.ARRAY_VERTEX].size() / 3)
	return tris

# Bone weights ride the mesh; the skeleton and skin come from the runtime export.
func _has_bones(m: Mesh) -> bool:
	for si in m.get_surface_count():
		if m.surface_get_arrays(si)[Mesh.ARRAY_BONES] != null:
			return true
	return false

func _write_json(path: String, data: Dictionary) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		printerr("FATAL: cannot write ", path)
		return
	f.store_string(JSON.stringify(data, "  "))
