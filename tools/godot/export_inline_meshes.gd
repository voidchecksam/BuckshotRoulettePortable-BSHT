# Export the geometry of meshes that have no name.
#
# scene_graph.json identifies each MeshInstance3D by mesh.resource_name. Nodes
# authored directly in the scene (PlaneMesh, BoxMesh, CylinderMesh, QuadMesh,
# unnamed ArrayMesh surfaces) have none, so they export as "RID(...)" and join
# to nothing in the extracted .glb library.
#
# The parametric primitives are not re-derived from their size parameters
# here. Godot has already tessellated them, and surface_get_arrays gives the
# same vertices the game draws, so a PlaneMesh and an inline ArrayMesh come out
# through one code path, and there is no second implementation of Godot's
# primitive generation to drift from the original.
#
# Run:
#   godot --headless --path <proj> --script res://tools/godot/export_inline_meshes.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")

const TextureExport := preload("res://tools/godot/texture_export.gd")

var OUT_PATH := SceneArgs.extracted("inline_meshes.json")

func _initialize() -> void:
	var scene: PackedScene = load(SceneArgs.scene_path())
	if scene == null:
		print("FATAL: could not load main.tscn"); quit(1); return
	var root: Node = scene.instantiate()
	if root == null:
		print("FATAL: could not instantiate main.tscn"); quit(1); return
	get_root().add_child(root)

	var out: Array = []
	var totals := {"nodes": 0, "surfaces": 0, "vertices": 0, "triangles": 0}
	var skipped: Array = []
	var by_class := {}

	_walk(root, root, out, totals, skipped, by_class)

	var data := {
		"meshes": out,
		"totals": totals,
		"skipped": skipped,
		"by_class": by_class,
	}

	var f := FileAccess.open(SceneArgs.out(OUT_PATH), FileAccess.WRITE)
	if f == null:
		print("FATAL: cannot write ", SceneArgs.out(OUT_PATH)); quit(1); return
	f.store_string(JSON.stringify(data))
	f.close()

	print("inline meshes=", totals["nodes"], " surfaces=", totals["surfaces"],
		  " vertices=", totals["vertices"], " triangles=", totals["triangles"])
	print("by class: ", by_class)
	if skipped.size() > 0:
		# Never silent: a skipped mesh is a hole in the room, and the whole
		# point of this exporter is to stop having those.
		print("WARNING skipped ", skipped.size(), ": ", skipped.slice(0, 5))
	print("wrote ", ProjectSettings.globalize_path(SceneArgs.out(OUT_PATH)))
	root.free()
	quit(0)

func _walk(n: Node, root: Node, out: Array, totals: Dictionary,
		   skipped: Array, by_class: Dictionary) -> void:
	if n is MeshInstance3D and n.mesh != null:
		# Only the UNNAMED ones. Named meshes come from the .glb library and are
		# already handled by the mesh-name join table.
		if String(n.mesh.resource_name) == "":
			var path := String(root.get_path_to(n))
			var cls: String = n.mesh.get_class()
			by_class[cls] = int(by_class.get(cls, 0)) + 1

			var surfaces := _export_surfaces(n.mesh, path, skipped, n)
			if surfaces.size() > 0:
				var verts := 0
				var tris := 0
				for s in surfaces:
					verts += int(s["positions"].size()) / 3
					tris += int(s["indices"].size()) / 3
				totals["nodes"] = int(totals["nodes"]) + 1
				totals["surfaces"] = int(totals["surfaces"]) + surfaces.size()
				totals["vertices"] = int(totals["vertices"]) + verts
				totals["triangles"] = int(totals["triangles"]) + tris
				out.append({
					"path": path,
					"class": cls,
					"surfaces": surfaces,
				})

	for c in n.get_children():
		_walk(c, root, out, totals, skipped, by_class)

# Albedo for one surface.
#
# These nodes carry no glTF material, so without this the room baker has
# nothing to texture them with and draws them white (the PlaneMeshes as bright
# bands across the room, which read as a lighting bug).
#
# Only the flat albedo colour and the texture's own path are taken: Godot's
# StandardMaterial3D has dozens of parameters, and the port's surface is one
# texture fetch and a multiply.
func _material_of(node: Node, mesh: Mesh, si: int) -> Dictionary:
	var mat: Material = null
	# Override first: a MeshInstance3D may replace the mesh's own material, and
	# the override is what actually renders.
	if node is MeshInstance3D:
		mat = (node as MeshInstance3D).get_active_material(si)
	if mat == null:
		mat = mesh.surface_get_material(si)

	var out := {
		"albedo": [1.0, 1.0, 1.0, 1.0],
		"texture": "",
		"texture_file": "",
		"transparent": false,
		"unshaded": false,
		"emission": [0.0, 0.0, 0.0],
	}
	if mat == null:
		return out
	if mat is StandardMaterial3D:
		var sm := mat as StandardMaterial3D
		var c: Color = sm.albedo_color
		out["albedo"] = [c.r, c.g, c.b, c.a]
		if sm.albedo_texture != null:
			out["texture"] = sm.albedo_texture.resource_path
			out["texture_file"] = _save_texture(sm.albedo_texture)
		out["transparent"] = sm.transparency != BaseMaterial3D.TRANSPARENCY_DISABLED
		out["unshaded"] = sm.shading_mode == BaseMaterial3D.SHADING_MODE_UNSHADED
		# The health counter's icons and lines are white albedo that EMITS green
		# (main.tscn sub_resource 234). Godot adds emission * energy on top of
		# the lit albedo.
		if sm.emission_enabled:
			var e: Color = sm.emission * sm.emission_energy_multiplier
			out["emission"] = [e.r, e.g, e.b]
	return out

# The texture as a PNG under assets/extracted/inline_textures (texture_export.gd).
# Some of these textures are embedded in main.tscn ("res://scenes/main.tscn::233"
# is the lightning icon) and have no file of their own, so every texture goes
# through get_image(), one path for both. Never packaged.

func _save_texture(tex: Texture2D) -> String:
	return TextureExport.save(tex)

func _export_surfaces(mesh: Mesh, path: String, skipped: Array,
		node: Node = null) -> Array:
	var surfaces: Array = []

	for si in range(mesh.get_surface_count()):
		# surface_get_primitive_type exists on ArrayMesh only. A PrimitiveMesh
		# (PlaneMesh, BoxMesh, ...) is always triangles, so there is nothing to
		# check, and calling it there is a hard error, not a false answer.
		if mesh is ArrayMesh:
			if mesh.surface_get_primitive_type(si) != Mesh.PRIMITIVE_TRIANGLES:
				skipped.append(path + " surface " + str(si) + " (not triangles)")
				continue

		var arrays: Array = mesh.surface_get_arrays(si)
		if arrays.is_empty():
			skipped.append(path + " surface " + str(si) + " (no arrays)")
			continue

		var verts = arrays[Mesh.ARRAY_VERTEX]
		if verts == null or verts.size() == 0:
			skipped.append(path + " surface " + str(si) + " (no vertices)")
			continue

		var pos: Array = []
		pos.resize(verts.size() * 3)
		for i in range(verts.size()):
			pos[i * 3 + 0] = verts[i].x
			pos[i * 3 + 1] = verts[i].y
			pos[i * 3 + 2] = verts[i].z

		var nrm: Array = []
		var normals = arrays[Mesh.ARRAY_NORMAL]
		if normals != null and normals.size() == verts.size():
			nrm.resize(normals.size() * 3)
			for i in range(normals.size()):
				nrm[i * 3 + 0] = normals[i].x
				nrm[i * 3 + 1] = normals[i].y
				nrm[i * 3 + 2] = normals[i].z

		var uv: Array = []
		var uvs = arrays[Mesh.ARRAY_TEX_UV]
		if uvs != null and uvs.size() == verts.size():
			uv.resize(uvs.size() * 2)
			for i in range(uvs.size()):
				uv[i * 2 + 0] = uvs[i].x
				uv[i * 2 + 1] = uvs[i].y

		# Godot allows a surface with no index array, meaning "vertices in
		# order". Writing that out explicitly means the C++ side has exactly one
		# path to maintain rather than two.
		var idx: Array = []
		var indices = arrays[Mesh.ARRAY_INDEX]
		if indices != null and indices.size() > 0:
			idx.resize(indices.size())
			for i in range(indices.size()):
				idx[i] = indices[i]
		else:
			idx.resize(verts.size())
			for i in range(verts.size()):
				idx[i] = i

		if verts.size() > 65535:
			# GLES2 has no 32-bit index path (trap 7). Loud, because the mesh
			# would otherwise be unindexable on device.
			skipped.append(path + " surface " + str(si) + " (>65535 vertices)")
			continue

		var material := _material_of(node, mesh, si) if node != null else {}
		surfaces.append({
			"material": material,
			"positions": pos,
			"normals": nrm,
			"uvs": uv,
			"indices": idx,
		})

	return surfaces
