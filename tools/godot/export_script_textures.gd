# Textures a script assigns to a material's albedo_texture at runtime:
# EndlessMode.displayrounds and CrtIcon.texture_active / texture_inactive. The
# scene's own materials only carry the texture they load with, so the bake has
# no PNG for the swapped-in ones. Each is saved through texture_export.gd under
# the key the port binds (godot::TextureKeyForResource derives the same name).
#
# Loads resources only: no scene is instantiated, no game code runs. Run it
# under a private user:// all the same (tools/godot/run_private.sh).
#
#   godot --headless --path <proj> --script res://tools/godot/export_script_textures.gd
extends SceneTree

const TextureExport := preload("res://tools/godot/texture_export.gd")
const SceneArgs := preload("res://tools/godot/scene_args.gd")
var RUNTIME := SceneArgs.extracted("runtime_main.json")
const FIELDS := {
	"EndlessMode": ["displayrounds"],
	"CrtIcon": ["texture_active", "texture_inactive"],
	# The four frames BloodSplatter swaps onto the blood splatter plane.
	"ShotgunShooting": ["splatters"],
}

func _initialize() -> void:
	var data = JSON.parse_string(FileAccess.get_file_as_string(RUNTIME))
	if data == null:
		print("FATAL: cannot read ", RUNTIME)
		quit(1)
		return
	var saved := 0
	for n in data["nodes"]:
		var script: String = n.get("script", "")
		if not FIELDS.has(script):
			continue
		var ex: Dictionary = n.get("exports", {})
		for field in FIELDS[script]:
			var v = ex.get(field)
			# A single resource, or an array of them (splatters).
			var items: Array = v if v is Array else [v]
			for item in items:
				if not (item is Dictionary) or not item.has("res"):
					print("WARNING: ", n["path"], ".", field, " has no resource")
					continue
				var tex: Texture2D = load(item["res"])
				var key: String = TextureExport.save(tex)
				if key == "":
					print("FATAL: could not save ", item["res"])
					quit(1)
					return
				print(key)
				saved += 1
	print(saved, " script textures saved")
	if saved == 0:
		printerr("FATAL: no script textures saved")
		quit(1)
		return
	quit()
