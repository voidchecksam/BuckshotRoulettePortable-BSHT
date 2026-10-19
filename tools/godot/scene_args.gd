# The scene an exporter reads, the suffix its outputs carry, and where they go.
#
#   godot ... --script res://tools/godot/export_ui.gd -- scenes/menu.tscn menu --out=/abs/dir
#
# reads res://scenes/menu.tscn and writes /abs/dir/ui_layout_menu.json. With no
# arguments an exporter reads main.tscn and writes under res://assets/extracted.
extends RefCounted

static func _positional() -> PackedStringArray:
	var out := PackedStringArray()
	for a in OS.get_cmdline_user_args():
		if not String(a).begins_with("--"):
			out.append(a)
	return out

static func scene_path() -> String:
	var args := _positional()
	if args.size() >= 2:
		return "res://" + String(args[0])
	return "res://scenes/main.tscn"

static func suffix() -> String:
	var args := _positional()
	if args.size() >= 2 and String(args[1]) != "main":
		return "_" + String(args[1])
	return ""

static func out_root() -> String:
	for a in OS.get_cmdline_user_args():
		if String(a).begins_with("--out="):
			return String(a).trim_prefix("--out=")
	return "res://assets/extracted"

# A file under the output root: extracted("models/_mesh_names.json").
static func extracted(name: String) -> String:
	return out_root().path_join(name)

# A language's dialogue table, named by its locale: "ES LATAM" -> .../dialogue_es_latam.json.
# Not by the translation file, whose sheet name changes between game versions.
static func dialogue_file(locale: String) -> String:
	return extracted("dialogue_" + locale.to_lower().replace(" ", "_") + ".json")

# extracted("ui_layout.json") with the scene's suffix: ".../ui_layout_menu.json"
static func out(path: String) -> String:
	var s := suffix()
	if s == "":
		return path
	return path.get_basename() + s + "." + path.get_extension()
