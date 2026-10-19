# Export every language's dialogue table.
#
# Every line the dealer says goes through tr("KEY"). The keys are in the
# GDScript; the text is in a compiled .translation resource, so the scripts say
# a line exists and not one word of it.
#
# The key list comes from the scripts and scenes, not the resource: the shipped
# tables are OptimizedTranslations, hash maps with the keys discarded, so
# get_message_list() returns nothing. Asking one for a key it holds works;
# enumerating is the one thing it cannot do. Keys come from:
#   - tr("...") in the scripts (KEYS below; regenerate with
#     grep -rhoE 'tr\("[^"]+"\)' scripts/ instances/ | sed 's/tr("//; s/")//' | sort -u)
#     plus "DOUBLE UNLOCK", which DonUnlockManager.gd passes through a variable
#     (tr(t)), so the grep cannot see it
#   - the item names and descriptions PickupIndicator writes (scene_strings.json)
#   - every Label/Button/Label3D text in main (ui_layout.json, labels3d.json)
#     and in menu.tscn, instantiated here: Controls translate their own text
#     (label.cpp set_text: xl_text = atr(text)), so a scene text is a key when a
#     table holds it.
#
# What is written is what Godot displays. All 17 tables are added to the
# TranslationServer and, per locale, each key goes through set_locale +
# TranslationServer.translate, so Godot's own fallback rule (a key missing from
# a table) is captured, not reimplemented. Keys no table resolves are left out;
# the port's tr() then returns the key, as Godot does.
#
# Output: assets/extracted/dialogue_<code>.json, code = the sheet's name
# lowercased with spaces as underscores (en, fr, es_latam, zhs, ...).
#
# Run:
#   godot --headless --path <proj> --script res://tools/godot/export_dialogue.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")
var SCENE_STRINGS := SceneArgs.extracted("scene_strings.json")
var UI_LAYOUT := SceneArgs.extracted("ui_layout.json")
var LABELS3D := SceneArgs.extracted("labels3d.json")
const MENU := "res://scenes/menu.tscn"
const HEAVEN := "res://scenes/heaven.tscn"

const KEYS := [
	"ARE YOU READY", "BETTER NOT", "BLANKROUND", "BLANKROUNDS", "CAREFUL",
	"CIGS SMOKED", "CONGRATULATIONS", "DOORS KICKED", "DOUBLE UNLOCK", "DRILL",
	"FINAL SHOW1", "FINAL SHOW2", "FINAL SHOW3", "GET UP", "HOLD TO EXIT",
	"INTERESTING", "ITEMS EACH", "LIVEROUND", "LIVEROUNDS", "ML DRANK",
	"MORE INTERESTING", "MORE ITEMS", "NO SPACE", "PLAYERWIN",
	"PRESS ANY BUTTON REMAP", "ROUNDS BEAT", "SEQUENCE2", "SEQUENCE3",
	"SEQUENCE4", "SEQUENCE5", "SEQUENCE6", "SEQUENCE7", "SHELL INSERT1",
	"SHELL INSERT2", "SHELLS EJECTED", "SHOTS FIRED", "THE NIGHT",
	"TOTAL CASH", "UNFORTUNATE", "WAIVER", "WELCOME", "YOURE LUCKY",
]

func _add_key(keys: Array, v) -> void:
	if v is String and v != "" and not keys.has(v):
		keys.append(v)

func _collect_menu_texts(n: Node, keys: Array) -> void:
	if n is Label or n is Button or n is Label3D or n is RichTextLabel:
		_add_key(keys, n.text)
	for c in n.get_children():
		_collect_menu_texts(c, keys)

func _harvest_keys() -> Array:
	var keys: Array = KEYS.duplicate()
	var strings = JSON.parse_string(FileAccess.get_file_as_string(SCENE_STRINGS))
	if strings == null:
		print("FATAL: cannot read ", SCENE_STRINGS, " (run export_scene_strings.gd first)")
		return []
	for path in strings["nodes"]:
		# "pickup indicator" and the shotgun's "pickup indicator branch"
		if not String(path).get_file().begins_with("pickup indicator"):
			continue
		for field in ["itemName", "itemDescription"]:
			_add_key(keys, strings["nodes"][path].get(field))
	# The item scenes' own pickup indicators: the dealt items are instanced from
	# instances/*.tscn, not main.tscn, so their CIGARETTE / CIGARETTE DESC, BEER,
	# HANDCUFFS, MAGNIFIER, PHONE keys are in no main export. bsht-extract runs
	# export_runtime_scene.gd on every item scene first, which writes these files.
	var items := 0
	for f in DirAccess.get_files_at(SceneArgs.out_root()):
		if not (f.begins_with("runtime_item_") and f.ends_with(".json")):
			continue
		var item = JSON.parse_string(FileAccess.get_file_as_string(SceneArgs.extracted(f)))
		if item == null:
			print("WARNING: cannot read ", f, " -- its item texts are not harvested")
			continue
		for n in item["nodes"]:
			if n.get("script") == "PickupIndicator":
				items += 1
				for field in ["itemName", "itemDescription"]:
					_add_key(keys, n["exports"].get(field))
	print(items, " item pickup indicators harvested")
	var ui = JSON.parse_string(FileAccess.get_file_as_string(UI_LAYOUT))
	if ui != null:
		for c in ui["controls"]:
			_add_key(keys, c.get("text"))
	var l3 = JSON.parse_string(FileAccess.get_file_as_string(LABELS3D))
	if l3 != null:
		for l in l3["labels"]:
			_add_key(keys, l.get("authored_text"))
	# heaven.tscn: RETRY, EXIT, DEAD, authored keys like the menu's.
	var heaven: PackedScene = load(HEAVEN)
	if heaven != null:
		var hroot := heaven.instantiate()
		_collect_menu_texts(hroot, keys)
		hroot.free()
	else:
		print("WARNING: cannot load ", HEAVEN, " -- its texts are not harvested")
	var menu: PackedScene = load(MENU)
	if menu != null:
		var root := menu.instantiate()
		_collect_menu_texts(root, keys)
		root.free()
	else:
		print("WARNING: cannot load ", MENU, " -- its texts are not harvested")
	return keys

func _initialize() -> void:
	var keys := _harvest_keys()
	if keys.is_empty():
		quit(1); return

	var paths: PackedStringArray = ProjectSettings.get_setting("internationalization/locale/translations")
	var tables := []
	for p in paths:
		var t: Translation = load(p)
		if t == null:
			print("FATAL: could not load ", p); quit(1); return
		TranslationServer.add_translation(t)
		tables.append({"res": t, "sheet": t.locale})
	print(keys.size(), " candidate keys; ", tables.size(), " tables; fallback locale '",
		ProjectSettings.get_setting("internationalization/locale/fallback", "en"), "'")

	for entry in tables:
		var t: Translation = entry["res"]
		TranslationServer.set_locale(t.locale)
		var out := {}
		var own := 0
		var fell_back: Array = []
		var longest := 0
		for k in keys:
			var shown: String = TranslationServer.translate(k)
			if shown == k and t.get_message(k) == "":
				continue  # no table holds it: the port's tr() returns the key
			out[k] = shown
			if t.get_message(k) != "":
				own += 1
			else:
				fell_back.append(k)
			longest = max(longest, shown.length())
		var path := SceneArgs.dialogue_file(t.locale)
		var f := FileAccess.open(path, FileAccess.WRITE)
		if f == null:
			print("FATAL: cannot write ", path); quit(1); return
		f.store_string(JSON.stringify({
			"locale": t.locale,
			"sheet": entry["sheet"],
			"count": out.size(),
			"fell_back": fell_back,
			"longest": longest,
			"messages": out,
		}, "  "))
		f.close()
		print(path.get_file().rpad(24), " locale '", t.locale, "': ", out.size(), " keys (", own,
			" own", ", " + str(fell_back.size()) + " by Godot's fallback " + str(fell_back) if fell_back.size() > 0 else "",
			"), longest ", longest)
	quit()
