# The mouse cursor images -> assets/extracted/cursors/.
#
# CursorManager.SetCursorImage swaps between three images as the pointer moves
# over the table (point / hover / invalid), and project.godot sets a fourth as
# the window's default. Godot loads them as Texture2D; the port's desktop window
# builds an SDL cursor from the pixels, so it needs them decoded on disk.
#
# Raw RGBA, not PNG: assets/ links no image decoder (texture_pool.h: the
# device freads pixels a host tool decoded), and a cursor is no reason to put
# one in the shipping binary. Each cursor is written as <name>.raw,
# width * height * 4 bytes, top row first, with cursors.json holding the sizes.
# The PNGs are written beside them for looking at.
#
# They are game content: they land in assets/extracted (gitignored) and are
# never compiled into the binary.
#
# Run:
#   godot --headless --path <proj> --script res://tools/godot/export_cursors.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")
var OUT_DIR := SceneArgs.extracted("cursors")

# The three CursorManager exports, plus project.godot's
# display/mouse_cursor/custom_image.
const IMAGES := [
	"res://misc/cursor xp_point.png",
	"res://misc/cursor xp_hover.png",
	"res://misc/cursor xp_invalid bigger.png",
]

func _initialize() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))

	var wanted: Array = IMAGES.duplicate()
	var default_image: String = ProjectSettings.get_setting("display/mouse_cursor/custom_image", "")
	if default_image != "" and not wanted.has(default_image):
		wanted.append(default_image)

	var written := 0
	var manifest := {}
	for path in wanted:
		var tex: Texture2D = load(path)
		if tex == null:
			printerr("missing ", path)
			continue
		var img: Image = tex.get_image()
		if img == null:
			printerr("no image data for ", path)
			continue
		# The port's decoder reads straight RGBA8.
		img.convert(Image.FORMAT_RGBA8)
		var file_name := String(path).get_file()
		var out := "%s/%s" % [OUT_DIR, file_name]
		if img.save_png(ProjectSettings.globalize_path(out)) != OK:
			printerr("cannot write ", out)
			continue

		# The raw RGBA the port reads: width * height * 4 bytes, top row first.
		var raw := "%s/%s.raw" % [OUT_DIR, file_name.get_basename()]
		var f := FileAccess.open(raw, FileAccess.WRITE)
		if f == null:
			printerr("cannot write ", raw)
			continue
		f.store_buffer(img.get_data())
		f.close()

		manifest[file_name] = {"width": img.get_width(), "height": img.get_height(),
			"raw": "%s.raw" % file_name.get_basename()}
		written += 1
		print("wrote ", out, " and ", raw, " ", img.get_width(), "x", img.get_height())

	var index := FileAccess.open("%s/cursors.json" % OUT_DIR, FileAccess.WRITE)
	if index != null:
		index.store_string(JSON.stringify({"format": 1, "cursors": manifest}, "  "))
		index.close()

	print("cursors: %d of %d written" % [written, wanted.size()])
	quit()
