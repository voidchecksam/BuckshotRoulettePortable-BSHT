# textures/smoke.png, the texture every particle system draws, as 64 x 64 for
# the renderer: particle_smoke.rgba in the output root (u32 width, u32 height,
# then RGBA8).
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")
const SIZE := 64

func _init() -> void:
	var tex: Texture2D = load("res://textures/smoke.png")
	if tex == null:
		printerr("FATAL: res://textures/smoke.png missing")
		quit(1)
		return
	var img := tex.get_image()
	if img.is_compressed():
		img.decompress()
	img.clear_mipmaps()   # the imported texture has them; the file holds one level
	img.convert(Image.FORMAT_RGBA8)
	img.resize(SIZE, SIZE, Image.INTERPOLATE_LANCZOS)
	var path := SceneArgs.extracted("particle_smoke.rgba")
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		printerr("FATAL: cannot write ", path)
		quit(1)
		return
	f.store_32(SIZE)
	f.store_32(SIZE)
	f.store_buffer(img.get_data())
	print("wrote particle_smoke.rgba ", SIZE, "x", SIZE)
	quit(0)
