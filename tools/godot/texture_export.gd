# A Texture2D as a PNG under <out>/inline_textures, returned as the
# key the baker reads ("inline_textures/<name>.png"), "" if it cannot be saved.
# Shared by export_inline_meshes.gd (main.tscn's inline meshes) and
# export_runtime_scene.gd (packed scenes' materials: the item templates).
extends RefCounted

const SceneArgs := preload("res://tools/godot/scene_args.gd")

static func _dir() -> String:
	return SceneArgs.extracted("inline_textures")

static var _saved := {}

static func save(tex: Texture2D) -> String:
	if tex == null:
		return ""
	var key := tex.resource_path
	if _saved.has(key):
		return _saved[key]
	var img: Image = tex.get_image()
	if img == null:
		return ""
	# A normal map imported for S3TC is DXT5_RA_AS_RG: X in red, Y in ALPHA,
	# and the GPU reads green through a swizzle (texture_storage.cpp:1872,
	# swizzle_g = A). 4.1.4's decompress means to move alpha into green but
	# tests the format after set_data has already made it RGBA8
	# (image_decompress_squish.cpp:74), so the swap never happens. Do it here,
	# so the PNG holds what the shader samples.
	var ra_as_rg := img.get_format() == Image.FORMAT_DXT5_RA_AS_RG
	if img.is_compressed():
		img.decompress()
	if ra_as_rg:
		img.convert(Image.FORMAT_RGBA8)
		var px := img.get_data()
		for i in range(0, px.size(), 4):
			px[i + 1] = px[i + 3]
			px[i + 2] = 0
			px[i + 3] = 255
		img.set_data(img.get_width(), img.get_height(), img.has_mipmaps(), Image.FORMAT_RGBA8, px)
	var name := key.replace("res://", "").replace("/", "_").replace(":", "_").replace(" ", "_")
	if not name.ends_with(".png"):
		name += ".png"
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(_dir()))
	if img.save_png(_dir().path_join(name)) != OK:
		print("WARNING could not save ", key)
		return ""
	# The shipped RA_AS_RG normal maps carry Y = 0 in mip 0 and their real Y
	# from mip 1 on, and the GPU samples whichever mip the view asks for. The
	# bake blends the two by that lod, so mip 1 travels beside mip 0
	# ("<name>_mip1.png"); every deeper mip holds the same Y.
	if ra_as_rg and img.has_mipmaps() and img.get_width() >= 2 and img.get_height() >= 2:
		var data := img.get_data()
		var a := img.get_mipmap_offset(1)
		var b := img.get_mipmap_offset(2) if img.get_width() >= 4 and img.get_height() >= 4 else data.size()
		var mip1 := Image.create_from_data(img.get_width() / 2, img.get_height() / 2, false,
			Image.FORMAT_RGBA8, data.slice(a, b))
		if mip1.save_png(_dir().path_join(name.trim_suffix(".png") + "_mip1.png")) != OK:
			print("WARNING could not save mip 1 of ", key)
	var rel := "inline_textures/" + name
	_saved[key] = rel
	return rel
