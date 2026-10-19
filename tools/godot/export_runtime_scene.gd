# Export the live state of main.tscn (after _ready has run and frames have
# passed) as the C++ godot:: runtime loads it.
#
# Every node in depth-first pre-order with its parent index, so the loader can
# build the tree in one pass. Transforms are local: the exact basis and origin,
# plus the rotation (radians) and scale Godot reports, because a sheared basis
# has no TRS form and the runtime keeps the matrix until a script writes one.
#
# Run (fixed fps so dump_state.gd's load_main capture sees the same frame):
#   godot --headless --fixed-fps 60 --path <proj> --script res://tools/godot/export_runtime_scene.gd
# An instanced scene (PackedScene template for godot::PackedScene), same format:
#   ... --script res://tools/godot/export_runtime_scene.gd -- instances/shell.tscn assets/extracted/runtime_shell.json
extends SceneTree

const TextureExport := preload("res://tools/godot/texture_export.gd")
const SceneArgs := preload("res://tools/godot/scene_args.gd")

var OUT_PATH := SceneArgs.extracted("runtime_main.json")
var SCENE_PATH := "res://scenes/main.tscn"
var SCENE_NAME := "main"
# 2: material, AnimationPlayer, AudioStreamPlayer and OmniLight3D state.
# 3: Control text / visible_characters / scale / modulate, Label3D text,
#    MeshInstance3D mesh name, BaseMaterial3D resources inlined in exports.
# 4: Control focus mode / neighbours / global transform / size / position /
#    mouse_filter; CollisionObject3D layer and mask; CollisionShape3D disabled +
#    shape (box size, or concave faces in <out>_faces.bin); Camera3D near and
#    far.
const FORMAT_VERSION := 4

# dump_state.gd's load_main scenario captures on the same frame. Change both.
const CAPTURE_FRAME := 3

var _root: Node = null
var _frames := 0
# Concave shape triangles, float32 little-endian x,y,z per vertex, three per face.
var _faces_bin := PackedByteArray()
var _shape_counts := {}

func _initialize() -> void:
	seed(0)
	var args := SceneArgs._positional()
	if args.size() == 2:
		SCENE_PATH = "res://" + args[0]
		SCENE_NAME = args[0]
		OUT_PATH = SceneArgs.extracted(String(args[1]).get_file())
	print("loading ", SCENE_PATH, " (main.tscn is 137 MB, this takes a moment)...")
	var scene: PackedScene = load(SCENE_PATH)
	if scene == null:
		printerr("FAIL: could not load ", SCENE_PATH)
		quit(1); return
	_root = scene.instantiate()
	# In the tree, or _ready never runs and is_visible_in_tree lies.
	get_root().add_child(_root)

func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < CAPTURE_FRAME:
		return false
	_export()
	return true

func _export() -> void:
	var nodes: Array = []
	_walk(_root, -1, nodes)
	var data := {
		"format": FORMAT_VERSION,
		"scene": SCENE_NAME,
		# The instanced root keeps its own name ("shell parent"); every path
		# below is relative to it.
		"root_name": String(_root.name),
		"capture_frame": CAPTURE_FRAME,
		"collision_faces": OUT_PATH.get_file().get_basename() + "_faces.bin",
		"nodes": nodes,
	}
	var abs_path := ProjectSettings.globalize_path(OUT_PATH)
	DirAccess.make_dir_recursive_absolute(abs_path.get_base_dir())
	var f := FileAccess.open(abs_path, FileAccess.WRITE)
	if f == null:
		printerr("FAIL: cannot write ", abs_path)
		quit(1); return
	# full_precision: the default prints 14 digits and loses the 1e-4 tolerance.
	f.store_string(JSON.stringify(data, "", false, true))
	f.close()
	var faces_path := abs_path.get_basename() + "_faces.bin"
	var ff := FileAccess.open(faces_path, FileAccess.WRITE)
	if ff == null:
		printerr("FAIL: cannot write ", faces_path)
		quit(1); return
	ff.store_buffer(_faces_bin)
	ff.close()
	print("collision shapes: ", _shape_counts, " faces bytes=", _faces_bin.size())
	print("nodes=", nodes.size(), " wrote ", abs_path)
	quit(0)

func _walk(n: Node, parent_index: int, nodes: Array) -> void:
	var e := {
		"path": String(_root.get_path_to(n)),
		"parent": parent_index,
		"class": n.get_class(),
		"script": "",
	}
	var s: Script = n.get_script()
	if s != null:
		e["script"] = s.resource_path.get_file().get_basename()
	if n is Node3D:
		e["visible"] = n.visible
		e["xform"] = _xform(n.transform)
		e["rotation"] = [n.rotation.x, n.rotation.y, n.rotation.z]
		e["scale"] = [n.scale.x, n.scale.y, n.scale.z]
	if n is CanvasItem:
		e["visible"] = n.visible
	if n is Control:
		e["scale"] = [n.scale.x, n.scale.y]
		e["modulate"] = [n.modulate.r, n.modulate.g, n.modulate.b, n.modulate.a]
		# The ending's labels fade and wobble by these.
		e["self_modulate"] = [n.self_modulate.r, n.self_modulate.g, n.self_modulate.b, n.self_modulate.a]
		e["control_rotation"] = n.rotation
		# Focus navigation, hit-testing, TextShaker's position (format 4).
		e["focus_mode"] = n.focus_mode
		e["focus_neighbor"] = [String(n.focus_neighbor_left), String(n.focus_neighbor_top),
			String(n.focus_neighbor_right), String(n.focus_neighbor_bottom)]
		var r: Rect2 = n.get_global_rect()
		e["global_rect"] = [r.position.x, r.position.y, r.size.x, r.size.y]
		# Godot's focus-neighbour search and hit test transform the four corners of
		# Rect2(Point2(), size) by the full global transform (rotation, scale, pivot).
		var gx: Transform2D = n.get_global_transform()
		e["global_xform"] = [gx.x.x, gx.x.y, gx.y.x, gx.y.y, gx.origin.x, gx.origin.y]
		e["size"] = [n.size.x, n.size.y]
		e["position"] = [n.position.x, n.position.y]
		e["mouse_filter"] = n.mouse_filter
		e["clip_contents"] = n.clip_contents
	if n is Label or n is Button:
		e["text"] = n.text
	if n is Label:
		e["visible_characters"] = n.visible_characters
	if n is Label3D:
		e["text"] = n.text
		# TextInteraction dims and brightens the shooting choice through it.
		e["modulate"] = [n.modulate.r, n.modulate.g, n.modulate.b, n.modulate.a]
	if n is MeshInstance3D and n.mesh != null:
		e["mesh"] = n.mesh.resource_name
	# Skinning: a Skeleton3D's bones (rest and the pose now), and a skinned
	# mesh's skeleton and binds (bone, bind pose = inverse bind).
	if n is Skeleton3D:
		var bones := []
		for i in n.get_bone_count():
			bones.append({"name": n.get_bone_name(i), "parent": n.get_bone_parent(i),
				"rest": _xform(n.get_bone_rest(i)), "pose": _xform(n.get_bone_pose(i))})
		e["bones"] = bones
	if n is MeshInstance3D and n.skin != null:
		var binds := []
		for i in n.skin.get_bind_count():
			binds.append({"name": String(n.skin.get_bind_name(i)), "bone": n.skin.get_bind_bone(i),
				"pose": _xform(n.skin.get_bind_pose(i))})
		e["skin"] = {"skeleton": String(n.skeleton), "binds": binds}
	if n is VisualInstance3D:
		e["layers"] = n.layers
	if n is GeometryInstance3D:
		e["transparency"] = n.transparency
	if n is Camera3D:
		e["current"] = n.is_current()
		e["fov"] = n.fov
		e["cull_mask"] = n.cull_mask
		e["near"] = n.near
		e["far"] = n.far
	if n is CollisionObject3D:
		e["collision_layer"] = n.collision_layer
		e["collision_mask"] = n.collision_mask
	if n is CollisionShape3D:
		e["disabled"] = n.disabled
		var shape: Shape3D = n.shape
		var kind := "none" if shape == null else shape.get_class()
		_shape_counts[kind] = _shape_counts.get(kind, 0) + 1
		if shape is BoxShape3D:
			e["shape"] = {"type": "box", "size": [shape.size.x, shape.size.y, shape.size.z]}
		elif shape is CylinderShape3D:
			# The beer can. Without radius and height the runtime cannot build
			# it, and the whole item scene fails to load.
			e["shape"] = {"type": "cylinder", "radius": shape.radius, "height": shape.height}
		elif shape is ConcavePolygonShape3D:
			var faces: PackedVector3Array = shape.get_faces()
			var flat := PackedFloat32Array()
			flat.resize(faces.size() * 3)
			for i in faces.size():
				flat[i * 3] = faces[i].x
				flat[i * 3 + 1] = faces[i].y
				flat[i * 3 + 2] = faces[i].z
			e["shape"] = {"type": "concave", "backface_collision": shape.backface_collision,
				"offset": _faces_bin.size() / 12, "count": faces.size()}
			_faces_bin.append_array(flat.to_byte_array())
		elif shape != null:
			e["shape"] = {"type": shape.get_class()}
	if n is GeometryInstance3D:
		# Material state the renderer needs: the muzzle-flash cone is hidden only
		# by its material's albedo alpha (0) on an alpha-transparent material.
		var mats: Array = []
		if n is MeshInstance3D and n.mesh != null:
			for i in n.mesh.get_surface_count():
				mats.append(_material(n.get_active_material(i)))
		e["materials"] = mats
		e["cast_shadow"] = int(n.cast_shadow)
		if n.material_override != null:
			e["material_override"] = _material(n.material_override)
			# Which resource it is, along with its values: a script can hold the same
			# material and write to it (SegmentManager fades the cut barrel through
			# `mat`, which is this override), and both must be one object.
			if n.material_override.resource_path != "":
				e["material_override_res"] = n.material_override.resource_path
	# The Environment's adjustments: animator_pp animates the brightness, the
	# muzzle flash the contrast, and the bake grades at these.
	if n is WorldEnvironment and n.environment != null:
		var env: Environment = n.environment
		e["environment"] = {
			"adjustment_enabled": env.adjustment_enabled,
			"adjustment_brightness": env.adjustment_brightness,
			"adjustment_contrast": env.adjustment_contrast,
			"adjustment_saturation": env.adjustment_saturation,
			# The menu lights with an ambient colour; main's is 0.
			"ambient_light_source": int(env.ambient_light_source),
			"ambient_light_color": [env.ambient_light_color.r, env.ambient_light_color.g,
				env.ambient_light_color.b],
			"ambient_light_energy": env.ambient_light_energy,
			# The fog and the background behind it. Heaven is mostly fog
			# (exponential 0.0035, light colour x1.48, sky affected); the menu has
			# its own. Colours are sRGB as authored.
			"fog_enabled": env.fog_enabled,
			"fog_light_color": [env.fog_light_color.r, env.fog_light_color.g, env.fog_light_color.b],
			"fog_light_energy": env.fog_light_energy,
			"fog_density": env.fog_density,
			"fog_sky_affect": env.fog_sky_affect,
			"fog_height_density": env.fog_height_density,
			"background_mode": int(env.background_mode),
			"background_color": [env.background_color.r, env.background_color.g, env.background_color.b],
			"background_energy_multiplier": env.background_energy_multiplier,
		}
	if n is OmniLight3D:
		e["light_energy"] = n.light_energy
		e["omni_range"] = n.omni_range
		# What the surface bake needs. The colour is sRGB as authored; the bake
		# linearises it as light_storage.cpp:802 does.
		var lc: Color = n.light_color
		e["light"] = {
			"color": [lc.r, lc.g, lc.b],
			"omni_attenuation": n.omni_attenuation,
			"shadow_enabled": n.shadow_enabled,
			"light_size": n.light_size,
			"light_specular": n.light_specular,
			"light_negative": n.light_negative,
			"cull_mask": n.light_cull_mask,
		}
	if n is GPUParticles3D:
		# What decides when the renderer draws the global RNG (godot/particles.h).
		var box: AABB = n.visibility_aabb
		e["particles"] = {
			"emitting": n.emitting, "one_shot": n.one_shot, "lifetime": n.lifetime,
			"speed_scale": n.speed_scale, "fixed_fps": n.fixed_fps, "preprocess": n.preprocess,
			"aabb": [box.position.x, box.position.y, box.position.z, box.size.x, box.size.y, box.size.z],
		}
		# What the CPU billboards need to look like the original.
		e["particles"]["look"] = _particle_look(n)
	if n is AnimationPlayer:
		# Resumed, not restarted: by the capture frame autoplay clips have run.
		e["player"] = {
			"assigned": String(n.assigned_animation),
			"playing": n.is_playing(),
			"position": n.current_animation_position if String(n.assigned_animation) != "" else 0.0,
			"speed_scale": n.speed_scale,
			"autoplay": String(n.autoplay),
		}
	if n is AudioStreamPlayer or n is AudioStreamPlayer2D or n is AudioStreamPlayer3D:
		e["audio"] = {
			"stream": "" if n.stream == null else n.stream.resource_path.get_file(),
			"volume_db": n.volume_db,
			"pitch_scale": n.pitch_scale,
			"playing": n.playing,
			# A direct play() past this many live playbacks cuts the oldest.
			"max_polyphony": n.max_polyphony,
			# The bus it plays on ("music" carries the table's low-pass).
			"bus": String(n.bus),
		}
		# What Godot's spatial mix reads (src/godot/audio_spatial.h).
		if n is AudioStreamPlayer2D:
			e["audio"]["global_position"] = [n.global_position.x, n.global_position.y]
			e["audio"]["max_distance"] = n.max_distance
			e["audio"]["attenuation"] = n.attenuation
			e["audio"]["panning_strength"] = n.panning_strength
		elif n is AudioStreamPlayer3D:
			e["audio"]["unit_size"] = n.unit_size
			e["audio"]["max_db"] = n.max_db
			e["audio"]["max_distance"] = n.max_distance
			e["audio"]["attenuation_model"] = n.attenuation_model
			e["audio"]["panning_strength"] = n.panning_strength
			e["audio"]["attenuation_filter_db"] = n.attenuation_filter_db
			e["audio"]["attenuation_filter_cutoff_hz"] = n.attenuation_filter_cutoff_hz
			e["audio"]["doppler_tracking"] = n.doppler_tracking
	var ex := _exports(n)
	if not ex.is_empty():
		e["exports"] = ex
	var index := nodes.size()
	nodes.append(e)
	for c in n.get_children():
		_walk(c, index, nodes)

# @export fields only: STORAGE and EDITOR usage. Plain `var`s are runtime state.
func _exports(n: Node) -> Dictionary:
	var out := {}
	var s: Script = n.get_script()
	if s == null:
		return out
	for p in s.get_script_property_list():
		var usage: int = p["usage"]
		if (usage & PROPERTY_USAGE_STORAGE) == 0 or (usage & PROPERTY_USAGE_EDITOR) == 0:
			continue
		out[p["name"]] = _value(n.get(p["name"]), 0)
	return out

func _value(v, depth: int):
	match typeof(v):
		TYPE_NIL:
			return null
		TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING:
			return v
		TYPE_STRING_NAME, TYPE_NODE_PATH:
			return String(v)
		TYPE_VECTOR2:
			return {"v2": [v.x, v.y]}
		TYPE_VECTOR3:
			return {"v3": [v.x, v.y, v.z]}
		TYPE_COLOR:
			return {"color": [v.r, v.g, v.b, v.a]}
		TYPE_ARRAY, TYPE_PACKED_STRING_ARRAY, TYPE_PACKED_FLOAT32_ARRAY, TYPE_PACKED_INT32_ARRAY:
			var arr: Array = []
			for x in v:
				arr.append(_value(x, depth))
			return arr
		TYPE_OBJECT:
			if v == null:
				return null
			if v is Node:
				return {"node": String(_root.get_path_to(v))}
			if v is BaseMaterial3D:
				# ShellClass.mat_live / mat_blank: the albedo is what ApplyStatus shows.
				return {"res": v.resource_path, "material": _material(v)}
			if v is Resource:
				var rs: Script = v.get_script()
				if rs != null and depth < 2:
					var fields := {}
					for p in rs.get_script_property_list():
						if (int(p["usage"]) & PROPERTY_USAGE_STORAGE) == 0:
							continue
						fields[p["name"]] = _value(v.get(p["name"]), depth + 1)
					return {"resource": rs.resource_path.get_file().get_basename(), "fields": fields}
				return {"res": v.resource_path}
	# type_string() arrived after 4.1; the Variant.Type number is what 4.1 has.
	return {"unsupported": typeof(v)}

# BaseMaterial3D only. A ShaderMaterial reports transparency -1 so the loader
# draws it as before instead of guessing what its shader does with alpha.
func _material(m: Material) -> Dictionary:
	if m is BaseMaterial3D:
		var c: Color = m.albedo_color
		# Godot adds emission on top of the lit albedo. BlinkerBranch swaps in
		# `mat_emission red`, whose albedo is white and whose red is emission.
		var e := Color(0, 0, 0, 1)
		if m.emission_enabled:
			# 4.1 names it emission_energy_multiplier (material.cpp:1588);
			# `emission_energy` is only the shader parameter, and reading it
			# fails silently enough to export an EMPTY material.
			e = m.emission * m.emission_energy_multiplier
		var out := {
			"albedo": [c.r, c.g, c.b, c.a],
			"emission": [e.r, e.g, e.b],
			"transparency": int(m.transparency),
			# For the surface bake. Emission is also exported unmultiplied: its
			# colour is sRGB (source_color) and must be linearised before the energy
			# scales it, which the premultiplied value above no longer allows.
			"roughness": m.roughness,
			"metallic": m.metallic,
			"emission_color": [m.emission.r, m.emission.g, m.emission.b] if m.emission_enabled else [0.0, 0.0, 0.0],
			"emission_energy": m.emission_energy_multiplier if m.emission_enabled else 0.0,
			"shading_mode": int(m.shading_mode),
			"diffuse_mode": int(m.diffuse_mode),
			"specular_mode": int(m.specular_mode),
			# BLEND_MODE_ADD (1): the blood splatter plane adds its black-backed
			# texture over the view (textures/blood/blood layer0.tres).
			"blend_mode": int(m.blend_mode),
		}
		# The bake transcribes Burley diffuse, per-pixel shading. Anything else
		# is named here so it cannot slip through unhandled.
		if m.shading_mode != BaseMaterial3D.SHADING_MODE_PER_PIXEL or m.diffuse_mode != BaseMaterial3D.DIFFUSE_BURLEY:
			print("NOTE material not Burley/per-pixel: ", m.resource_path, " shading=", m.shading_mode, " diffuse=", m.diffuse_mode)
		# The texture Godot draws, when the scene's material sets one. An item
		# scene can give its mesh a material the .glb does not have: the
		# handcuffs' "metal sheet painted_handcuffs" texture exists only here
		# (without it they bake untextured, as lit felt-green rings).
		if m.albedo_texture != null:
			var key: String = TextureExport.save(m.albedo_texture)
			if key != "":
				out["albedo_texture"] = key
		# The normal map (material.cpp:1289-1291: NORMAL_MAP = texture(...).rgb,
		# NORMAL_MAP_DEPTH = normal_scale). 70 of main.tscn's materials carry
		# one; without it the defib cutters' cones were lit where Godot has them
		# black, and the counter frame read ~1.35x too bright.
		if m.normal_enabled and m.normal_texture != null:
			var nkey: String = TextureExport.save(m.normal_texture)
			if nkey != "":
				out["normal_texture"] = nkey
				out["normal_scale"] = m.normal_scale
		return out
	return {"albedo": [1.0, 1.0, 1.0, 1.0], "emission": [0.0, 0.0, 0.0], "transparency": -1}

func _curve(c: Curve) -> Array:
	var out := []
	for i in 9:
		out.append(c.sample_baked(i / 8.0) if c != null else 1.0)
	return out

func _ramp(t: Texture2D) -> Array:
	# A GradientTexture1D's gradient, 9 samples of RGBA; white when absent.
	var out := []
	var g: Gradient = t.gradient if t is GradientTexture1D else null
	for i in 9:
		var c: Color = g.sample(i / 8.0) if g != null else Color(1, 1, 1, 1)
		out.append([c.r, c.g, c.b, c.a])
	return out

func _particle_look(n: GPUParticles3D) -> Dictionary:
	var d := {"amount": n.amount, "explosiveness": n.explosiveness, "randomness": n.randomness,
		"local_coords": n.local_coords}
	var m = n.process_material
	if m is ParticleProcessMaterial:
		var pm: ParticleProcessMaterial = m
		var sc: Curve = pm.scale_curve.curve if pm.scale_curve is CurveTexture else null
		d["process"] = {
			"direction": [pm.direction.x, pm.direction.y, pm.direction.z], "spread": pm.spread,
			"velocity": [pm.initial_velocity_min, pm.initial_velocity_max],
			"gravity": [pm.gravity.x, pm.gravity.y, pm.gravity.z],
			"damping": [pm.damping_min, pm.damping_max],
			"scale": [pm.scale_min, pm.scale_max], "scale_curve": _curve(sc),
			"color": [pm.color.r, pm.color.g, pm.color.b, pm.color.a], "color_ramp": _ramp(pm.color_ramp),
			"emission_shape": int(pm.emission_shape), "sphere_radius": pm.emission_sphere_radius,
			"box_extents": [pm.emission_box_extents.x, pm.emission_box_extents.y, pm.emission_box_extents.z],
			"angle": [pm.angle_min, pm.angle_max],
		}
	var mesh: Mesh = n.draw_pass_1
	if mesh != null:
		var size := Vector2(1, 1)
		if mesh is QuadMesh or mesh is PlaneMesh:
			size = mesh.size
		var look := {"mesh": mesh.get_class(), "size": [size.x, size.y]}
		var mat = mesh.surface_get_material(0) if mesh.get_surface_count() > 0 else null
		if mat is BaseMaterial3D:
			var bm: BaseMaterial3D = mat
			look["albedo"] = [bm.albedo_color.r, bm.albedo_color.g, bm.albedo_color.b, bm.albedo_color.a]
			look["texture"] = bm.albedo_texture.resource_path if bm.albedo_texture != null else ""
			look["billboard"] = int(bm.billboard_mode)
			look["unshaded"] = bm.shading_mode == BaseMaterial3D.SHADING_MODE_UNSHADED
			look["vertex_color"] = bm.vertex_color_use_as_albedo
		d["draw"] = look
	return d

func _xform(t: Transform3D) -> Array:
	var b := t.basis
	return [
		b.x.x, b.x.y, b.x.z, 0.0,
		b.y.x, b.y.y, b.y.z, 0.0,
		b.z.x, b.z.y, b.z.z, 0.0,
		t.origin.x, t.origin.y, t.origin.z, 1.0,
	]
