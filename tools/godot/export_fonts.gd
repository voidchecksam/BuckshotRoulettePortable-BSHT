# Export the UI font atlas and per-glyph metrics, for every language.
#
# Godot rasterises, the port does not. The port could link stb_truetype and
# rasterise `fake receipt.otf` itself, and it would be nearly the same. Nearly
# is the problem: hinting, the rounding of advances and subpixel offsets all
# differ between rasterisers, and text a pixel out at every size is visible
# against reference frames. Godot has already built an atlas with the exact
# metrics the original draws with, so that atlas is what ships. No font
# library reaches the device, and neither do the .ttf/.otf files.
#
# Every language, as Godot draws it. Every game font is imported with
# allow_system_fallback=true and no scene uses fonts/fonts language/: a
# character a game font lacks (Cyrillic, Japanese, Korean, Chinese) is drawn by
# Godot's TextServer from a system font the OS supplies. So glyphs are not
# asked of the FontFile directly: each string of each locale's table is shaped
# through TextServer with the face's font, under that locale
# (TranslationServer.set_locale), as a Label shapes it, and every glyph is read
# back from whichever font RID the shaper chose, the game font or the system
# fallback. Run on Windows, it rasterises what a Windows player sees.
#
# Pages and groups. Pages are LA8, at most 1024x1024. Page group "base" holds
# every glyph any non-CJK locale needs (ASCII, Latin accents, Cyrillic) and is
# always resident. A CJK locale's glyphs that base lacks go to a group named
# for it (JA, KO, ZHS, ZHT): the port uploads that group only while the locale
# is selected. Page 0 is ui_font_atlas.bin, the page the loading screen reads.
#
# Which characters a face gets:
#   - a 2D face used by the main scene's dialogue UI: printable ASCII plus every
#     character of every table (its text is set at runtime);
#   - a 2D face used only by menu.tscn or heaven.tscn: printable ASCII plus the
#     translations of the texts its Labels/Buttons author (authored keys);
#   - a Label3D face: its labels' text and that text's translations, restricted
#     to what it draws (full ranges at 232 px would not fit).
# A Label3D draws its outline as a second pass from an outline-rasterised face,
# so labels with a visible outline also get "<path> @ <size> outline <n>".
#
# Pixels ship as LUMINANCE_ALPHA: Godot's cache is FORMAT_LA8 with luminance
# pinned to white and coverage in alpha, which is what GL_LUMINANCE_ALPHA wants.
#
# Run (after export_dialogue.gd):
#   godot --headless --path <proj> --script res://tools/godot/export_fonts.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")
var OUT_JSON := SceneArgs.extracted("ui_fonts.json")
const PAGE0_FILE := "ui_font_atlas.bin"

const UI_ROOT := "Camera/dialogue UI"
const SETTLE_FRAMES := 3

# Printable ASCII, dense in the loader. Inclusive range.
const FIRST_CHAR := 32
const LAST_CHAR := 126

const PAD := 1
const PAGE_EDGE := 1024

const CJK := ["JA", "KO", "ZHS", "ZHT"]

var _root: Node = null
var _menu: Node = null
var _heaven: Node = null
var _label3dText: Array = []
var _frames := 0
# locale -> Array of message strings, locale -> file code
var _tables := {}
var _locales: Array = []

func _initialize() -> void:
	var scene: PackedScene = load("res://scenes/main.tscn")
	if scene == null:
		print("FATAL: could not load main.tscn"); quit(1); return
	_root = scene.instantiate()
	get_root().add_child(_root)
	var menu: PackedScene = load("res://scenes/menu.tscn")
	if menu == null:
		print("FATAL: could not load menu.tscn"); quit(1); return
	_menu = menu.instantiate()
	# Only the theme resolution is wanted: the menu's scripts must not run
	# (MenuManager would start the intro and change scenes).
	_strip_scripts(_menu)
	get_root().add_child(_menu)
	# heaven.tscn: its death UI draws "DEAD" in a face no other scene uses, and
	# RETRY / EXIT at a size of their own. Treated as the menu.
	var heaven: PackedScene = load("res://scenes/heaven.tscn")
	if heaven == null:
		print("FATAL: could not load heaven.tscn"); quit(1); return
	_heaven = heaven.instantiate()
	_strip_scripts(_heaven)
	get_root().add_child(_heaven)

func _strip_scripts(n: Node) -> void:
	n.set_script(null)
	for c in n.get_children():
		_strip_scripts(c)

func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_export()
	quit()
	return true

func _load_tables() -> bool:
	var paths: PackedStringArray = ProjectSettings.get_setting("internationalization/locale/translations")
	for p in paths:
		var t: Translation = load(p)
		if t == null:
			print("FATAL: could not load ", p); return false
		TranslationServer.add_translation(t)
		var path := SceneArgs.dialogue_file(t.locale)
		var d = JSON.parse_string(FileAccess.get_file_as_string(path))
		if d == null:
			print("FATAL: ", path, " missing -- run export_dialogue.gd first"); return false
		_tables[t.locale] = (d["messages"] as Dictionary)
		_locales.append(t.locale)
	# Non-CJK first: base collects their characters before any CJK group looks.
	_locales.sort_custom(func(a, b): return int(CJK.has(a)) < int(CJK.has(b)))
	return true

# Label3Ds a script writes the typed waiver name into at runtime, with the tr()
# key the name is formatted into ("" = the name alone). Their export-time text
# ("", "PLAYER WINS") holds too few letters to draw a typed name.
const _NAME_LABELS := {
	"tabletop parent/main tabletop/health counter/health counter ui parent/health UI_player side/text_player": "",
	"tabletop parent/main tabletop/health counter/health counter ui parent/indicator_player win": "PLAYERWIN",
}

# Label3Ds the Double or Nothing scripts write at runtime, by path prefix: the
# endless score (RoundManager.LerpScore), the CRT's partition counters
# (PartitionBranch), the local leaderboard rows and "(rank/count)"
# (LeaderboardManager: "#rank NAME-", scores, "<null>") and the lifetime stats.
const _RUNTIME_LABEL_PREFIXES := [
	"tabletop parent/main tabletop/health counter/health counter ui parent/indicator_endless score",
	"restroom_CLUB/bathroom wall main_crt hole/crt main parent/crt screen main/",
]
const _RUNTIME_LABEL_CHARS := "0123456789#'()/<>-.: ABCDEFGHIJKLMNOPQRSTUVWXYZnul"

func _translations_of(text: String) -> Dictionary:
	# locale -> what a Control/Label3D authored with `text` shows in it.
	var out := {}
	for loc in _locales:
		var m: Dictionary = _tables[loc]
		out[loc] = String(m.get(text, text))
	return out

func _export() -> void:
	if not _load_tables():
		quit(1); return
	var ui := _root.get_node_or_null(NodePath(UI_ROOT))
	if ui == null:
		print("FATAL: no node at ", UI_ROOT); quit(1); return

	var wanted: Array = []
	var seen := {}
	_collect(ui, wanted, seen, true)
	_collect(_menu, wanted, seen, false)
	_collect(_heaven, wanted, seen, false)
	_collect3d(_root, wanted, seen)
	if wanted.is_empty():
		print("FATAL: no Labels resolved to a font"); quit(1); return
	print("fonts: ", wanted.size(), " faces wanted")
	for w in wanted:
		print("  wanted ", w["key"], " (", (w["texts"] as Array).size(), " texts)")

	var faces: Array = []
	var glyphImages := {}   # group -> Array of {img, face, list, index}
	var missing := {}       # locale -> {char: true}
	for entry in wanted:
		var face := _measure_face(entry, faces.size(), glyphImages, missing)
		if face.is_empty():
			print("WARNING: no glyphs rendered for ", entry["key"])
			continue
		faces.append(face)

	var pages: Array = []
	var groupOrder: Array = ["base"]
	for g in CJK:
		if glyphImages.has(g):
			groupOrder.append(g)
	for g in groupOrder:
		_pack_group(g, glyphImages.get(g, []), faces, pages)

	var pageInfo: Array = []
	var bytesByGroup := {}
	var pagesByGroup := {}
	for i in range(pages.size()):
		var p: Dictionary = pages[i]
		var file: String = PAGE0_FILE if i == 0 else "ui_font_page_%s_%d.bin" % [String(p["group"]).to_lower(), int(p["n"])]
		var pf := FileAccess.open(SceneArgs.extracted(file), FileAccess.WRITE)
		if pf == null:
			print("FATAL: cannot write ", file); quit(1); return
		pf.store_buffer(p["pixels"])
		pf.close()
		pageInfo.append({"group": p["group"], "file": file, "width": p["width"], "height": p["height"]})
		bytesByGroup[p["group"]] = int(bytesByGroup.get(p["group"], 0)) + (p["pixels"] as PackedByteArray).size()
		pagesByGroup[p["group"]] = int(pagesByGroup.get(p["group"], 0)) + 1

	var missingOut := {}
	for loc in missing:
		missingOut[loc] = (missing[loc] as Dictionary).keys()
	var out := {
		"first_char": FIRST_CHAR,
		"last_char": LAST_CHAR,
		"atlas_width": pageInfo[0]["width"],
		"atlas_height": pageInfo[0]["height"],
		"pages": pageInfo,
		"faces": faces,
		"label3d_text": _label3dText,
		"missing": missingOut,
		"stats": {
			"n_faces": faces.size(),
			"n_label3d": _label3dText.size(),
			"pages_by_group": pagesByGroup,
			"bytes_by_group": bytesByGroup,
		},
	}
	var jf := FileAccess.open(OUT_JSON, FileAccess.WRITE)
	if jf == null:
		print("FATAL: cannot write ", OUT_JSON); quit(1); return
	jf.store_string(JSON.stringify(out, "  "))
	jf.close()

	print("faces: ", faces.size())
	for g in groupOrder:
		print("  group %-4s %d page(s)  %.2f MB" % [g, int(pagesByGroup.get(g, 0)),
			float(bytesByGroup.get(g, 0)) / 1048576.0])
	if pagesByGroup.get("base", 0) > 1:
		print("  NOTE: base needs more than one page")
	for loc in missing:
		print("  locale ", loc, ": ", (missing[loc] as Dictionary).size(), " character(s) no font draws: ",
			(missing[loc] as Dictionary).keys().slice(0, 20))
	for face in faces:
		var ng := 0
		for gname in (face["groups"] as Dictionary):
			ng += (face["groups"][gname] as Array).size()
		print("  %-46s ascii %3d  extra %4d  cjk %5d" % [face["key"], int(face["glyph_count"]),
			(face["extra"] as Array).size(), ng])
	print("wrote ", OUT_JSON, " and ", pageInfo.size(), " page file(s)")

# 2D Labels and Buttons. `dynamic` faces (the main scene's UI, whose text is set
# at runtime) get every table's characters; menu faces get their own texts'.
func _collect(n: Node, wanted: Array, seen: Dictionary, dynamic: bool) -> void:
	if n is Label or n is Button:
		var c := n as Control
		var f: Font = c.get_theme_font("font")
		var size: int = c.get_theme_font_size("font_size")
		var file := _as_font_file(f)
		if file != null and size > 0:
			var path: String = file.resource_path
			if path == "":
				path = "(embedded)"
			var key := "%s @ %d" % [path, size]
			var texts: Array = []
			var authored: String = n.text
			if authored != "":
				var tl := _translations_of(authored)
				for loc in tl:
					texts.append({"locale": loc, "text": tl[loc]})
			if seen.has(key):
				var e: Dictionary = wanted[int(seen[key])]
				e["labels"] += 1
				e["dynamic"] = bool(e["dynamic"]) or dynamic
				(e["texts"] as Array).append_array(texts)
			else:
				seen[key] = wanted.size()
				wanted.append({
					"key": key, "path": path, "font": file, "size": size, "labels": 1,
					"outline": 0, "label3d": false, "dynamic": dynamic, "texts": texts,
				})
	for c in n.get_children():
		_collect(c, wanted, seen, dynamic)

# Every Label3D in the main scene; `texts` is what each face must draw, in each
# locale (a Label3D translates its text: label_3d.cpp set_text, xl_text = atr).
func _collect3d(n: Node, wanted: Array, seen: Dictionary) -> void:
	if n is Label3D:
		var l := n as Label3D
		var f: Font = l.font
		if f == null:
			f = ThemeDB.fallback_font
		var file := _as_font_file(f)
		var size: int = l.font_size
		if file != null and size > 0:
			var path: String = file.resource_path
			if path == "":
				path = "(embedded)"
			_label3dText.append({"path": String(_root.get_path_to(l)), "text": l.text})
			var texts: Array = []
			var tl := _translations_of(l.text)
			for loc in tl:
				var t: String = tl[loc]
				if l.uppercase:
					t = t.to_upper()
				# An EMPTY set would mean the full range. A Label3D with no text
				# must not fall into that.
				texts.append({"locale": loc, "text": t if t != "" else " "})
			# Labels SignatureManager.gd writes at runtime: the machine's display
			# gets the typed letter aliases (Input_Letter, lower case), the waiver's
			# signature their to_upper() (Input_Enter). Their export-time text
			# ("PLAYER", "") would leave the face without the letters to draw.
			var ln := String(l.name)
			var lp := String(_root.get_path_to(l))
			if _NAME_LABELS.has(lp):
				# HealthCounter.SetupHealth writes the waiver name, upper case
				# (and the win line as tr("PLAYERWIN") % NAME).
				texts.append({"locale": "EN", "text": "ABCDEFGHIJKLMNOPQRSTUVWXYZ"})
				if _NAME_LABELS[lp] != "":
					var tw := _translations_of(_NAME_LABELS[lp])
					for loc in tw:
						texts.append({"locale": loc, "text": String(tw[loc]).replace("%s", "")})
			elif _is_runtime_label(lp):
				texts.append({"locale": "EN", "text": _RUNTIME_LABEL_CHARS})
			elif ln.begins_with("letter_signed_"):
				texts.append({"locale": "EN", "text": "ABCDEFGHIJKLMNOPQRSTUVWXYZ"})
			elif ln.begins_with("letter_"):
				texts.append({"locale": "EN", "text": "abcdefghijklmnopqrstuvwxyz"})
			_want3d(wanted, seen, "%s @ %d" % [path, size], path, file, size, texts, 0)
			if l.outline_modulate.a > 0.0 and l.outline_size > 0:
				_want3d(wanted, seen, "%s @ %d outline %d" % [path, size, l.outline_size],
					path, file, size, texts, l.outline_size)
	for c in n.get_children():
		_collect3d(c, wanted, seen)

func _is_runtime_label(path: String) -> bool:
	for prefix in _RUNTIME_LABEL_PREFIXES:
		if path.begins_with(prefix):
			return true
	return false

func _want3d(wanted: Array, seen: Dictionary, key: String, path: String,
		file: FontFile, size: int, texts: Array, outline: int) -> void:
	if seen.has(key):
		var e: Dictionary = wanted[int(seen[key])]
		e["labels"] += 1
		(e["texts"] as Array).append_array(texts)
		return
	seen[key] = wanted.size()
	wanted.append({
		"key": key, "path": path, "font": file, "size": size, "labels": 1,
		"outline": outline, "label3d": true, "dynamic": false, "texts": texts.duplicate(),
	})

func _as_font_file(f: Font) -> FontFile:
	# A theme may wrap the real file in a FontVariation; the glyph cache lives on
	# the FontFile underneath it, so unwrap before asking for anything.
	if f == null:
		return null
	if f is FontFile:
		return f as FontFile
	if f is FontVariation:
		return _as_font_file((f as FontVariation).base_font)
	return null

# The (locale, text) pairs a face is shaped with.
func _texts_for(entry: Dictionary) -> Array:
	var texts: Array = []
	var ascii := ""
	if not bool(entry["label3d"]):
		for c in range(FIRST_CHAR, LAST_CHAR + 1):
			ascii += String.chr(c)
		texts.append({"locale": "EN", "text": ascii})
	if bool(entry["dynamic"]):
		for loc in _locales:
			for k in _tables[loc]:
				texts.append({"locale": loc, "text": String(_tables[loc][k])})
	else:
		texts.append_array(entry["texts"])
	# Non-CJK first, so base owns every character they share with CJK.
	texts.sort_custom(func(a, b): return int(CJK.has(a["locale"])) < int(CJK.has(b["locale"])))
	return texts

func _measure_face(entry: Dictionary, faceIndex: int, glyphImages: Dictionary,
		missing: Dictionary) -> Dictionary:
	var f: FontFile = entry["font"]
	var size: int = entry["size"]
	var outline: int = entry["outline"]
	var key := Vector2i(size, outline)
	var ts := TextServerManager.get_primary_interface()
	var rids: Array = f.get_rids()

	var dense: Array = []
	for c in range(FIRST_CHAR, LAST_CHAR + 1):
		dense.append({"char": c, "present": false})
	var extra: Array = []
	var groups := {}
	var placed := {}   # "base:<cp>" / "JA:<cp>" -> true
	# A Label line is as tall as the fonts its glyphs come from
	# (text_server_adv.cpp _shape_substr), not Font.get_height, which takes
	# the tallest of every fallback. Row 0 is this font; a glyph from a
	# fallback names its row.
	var lineMetrics: Array = [[ts.font_get_ascent(rids[0], size), ts.font_get_descent(rids[0], size)]]
	var metricRow := {rids[0]: 0}

	for item in _texts_for(entry):
		var loc: String = item["locale"]
		var text: String = item["text"]
		TranslationServer.set_locale(loc)
		var shaped := ts.create_shaped_text()
		ts.shaped_text_add_string(shaped, text, rids, size)
		ts.shaped_text_shape(shaped)
		var glyphs: Array = ts.shaped_text_get_glyphs(shaped)
		for g in glyphs:
			var start: int = g["start"]
			if int(g["end"]) - start != 1 or int(g["count"]) != 1:
				continue  # clusters (combining marks) are not in these tables
			var cp := text.unicode_at(start)
			if cp < FIRST_CHAR:
				continue
			var group := "base"
			if CJK.has(loc) and not placed.has("base:%d" % cp):
				group = loc
			var slot := "%s:%d" % [group, cp]
			if placed.has(slot) or (group != "base" and placed.has("base:%d" % cp)):
				continue
			var idx: int = g["index"]
			var rid: RID = g["font_rid"]
			if idx == 0 or not rid.is_valid() or (int(g["flags"]) & TextServer.GRAPHEME_IS_VALID) == 0:
				if not missing.has(loc):
					missing[loc] = {}
				missing[loc][String.chr(cp)] = true
				continue
			placed[slot] = true
			var gl := _read_glyph(ts, rid, key, size, idx, cp)
			if not metricRow.has(rid):
				metricRow[rid] = lineMetrics.size()
				lineMetrics.append([ts.font_get_ascent(rid, size), ts.font_get_descent(rid, size)])
			if int(metricRow[rid]) > 0:
				gl["glyph"]["font"] = metricRow[rid]
			var list: Array
			var listIndex: int
			if group == "base" and cp <= LAST_CHAR:
				dense[cp - FIRST_CHAR] = gl["glyph"]
				list = dense
				listIndex = cp - FIRST_CHAR
			elif group == "base":
				extra.append(gl["glyph"])
				list = extra
				listIndex = extra.size() - 1
			else:
				if not groups.has(group):
					groups[group] = []
				(groups[group] as Array).append(gl["glyph"])
				list = groups[group]
				listIndex = (groups[group] as Array).size() - 1
			if gl["img"] != null:
				if not glyphImages.has(group):
					glyphImages[group] = []
				(glyphImages[group] as Array).append({"img": gl["img"], "list": list, "index": listIndex})
		ts.free_rid(shaped)

	var present := 0
	for g in dense:
		if g["present"]:
			present += 1
	if present == 0 and extra.is_empty() and groups.is_empty():
		return {}
	return {
		"key": entry["key"], "path": entry["path"], "size": size,
		"labels": entry["labels"], "outline": outline,
		"ascent": f.get_ascent(size),
		"descent": f.get_descent(size),
		"height": f.get_height(size),
		"line_metrics": lineMetrics,
		"glyph_count": present,
		"glyphs": dense,
		"extra": extra,
		"groups": groups,
	}

func _read_glyph(ts: TextServer, rid: RID, key: Vector2i, size: int, idx: int, cp: int) -> Dictionary:
	var uv: Rect2 = ts.font_get_glyph_uv_rect(rid, key, idx)
	var off: Vector2 = ts.font_get_glyph_offset(rid, key, idx)
	# Advance takes a plain int size and never includes kerning: the port's
	# layout adds advances.
	var adv: Vector2 = ts.font_get_glyph_advance(rid, size, idx)
	var glyph := {
		"char": cp, "present": true,
		"w": int(uv.size.x), "h": int(uv.size.y),
		"ox": off.x, "oy": off.y, "advance": adv.x,
		"x": 0, "y": 0, "page": 0,
	}
	if int(uv.size.x) <= 0 or int(uv.size.y) <= 0:
		return {"glyph": glyph, "img": null}
	var tex: int = ts.font_get_glyph_texture_idx(rid, key, idx)
	var page: Image = ts.font_get_texture_image(rid, key, tex)
	if page == null:
		return {"glyph": glyph, "img": null}
	page = page.duplicate()
	if page.is_compressed():
		page.decompress()
	page.convert(Image.FORMAT_RGBA8)
	var region: Image = page.get_region(Rect2i(int(uv.position.x), int(uv.position.y),
		int(uv.size.x), int(uv.size.y)))
	# A SPACE HAS A REAL REGION AND NO INK: it keeps its advance, loses its pixels.
	if not _has_ink(region):
		glyph["w"] = 0
		glyph["h"] = 0
		return {"glyph": glyph, "img": null}
	return {"glyph": glyph, "img": region}

func _has_ink(img: Image) -> bool:
	for y in range(img.get_height()):
		for x in range(img.get_width()):
			if img.get_pixel(x, y).a > 0.0:
				return true
	return false

# Shelf packing, tallest first, into as many PAGE_EDGE-wide pages as the group
# needs. A page's height is trimmed to the power of two its shelves reach.
func _pack_group(group: String, imgs: Array, faces: Array, pages: Array) -> void:
	imgs.sort_custom(func(a, b):
		return (a["img"] as Image).get_height() > (b["img"] as Image).get_height())
	var n := 0
	var start := 0
	while true:
		var x := PAD
		var y := PAD
		var rowH := 0
		var placed: Array = []
		var i := start
		while i < imgs.size():
			var img: Image = imgs[i]["img"]
			var w := img.get_width()
			var h := img.get_height()
			if x + w + PAD > PAGE_EDGE:
				x = PAD
				y += rowH + PAD
				rowH = 0
			if y + h + PAD > PAGE_EDGE:
				break
			placed.append({"e": imgs[i], "x": x, "y": y})
			x += w + PAD
			rowH = max(rowH, h)
			i += 1
		var used := y + rowH + PAD
		var height := 64
		while height < used:
			height *= 2
		if group == "base" and pages.is_empty() and imgs.size() > 0 and i < imgs.size():
			height = PAGE_EDGE
		var atlas := Image.create(PAGE_EDGE, height, false, Image.FORMAT_RGBA8)
		atlas.fill(Color(1, 1, 1, 0))
		var pageIndex := pages.size()
		for p in placed:
			var img: Image = p["e"]["img"]
			atlas.blit_rect(img, Rect2i(0, 0, img.get_width(), img.get_height()), Vector2i(p["x"], p["y"]))
			var g: Dictionary = (p["e"]["list"] as Array)[int(p["e"]["index"])]
			g["x"] = int(p["x"])
			g["y"] = int(p["y"])
			g["page"] = pageIndex
		var data := atlas.get_data()
		var la := PackedByteArray()
		la.resize(PAGE_EDGE * height * 2)
		for k in range(PAGE_EDGE * height):
			la[k * 2] = 255
			la[k * 2 + 1] = data[k * 4 + 3]
		pages.append({"group": group, "n": n, "width": PAGE_EDGE, "height": height, "pixels": la})
		n += 1
		start = i
		if start >= imgs.size():
			break
	# Glyphs with no ink carry the page of the group's first page, harmlessly.
