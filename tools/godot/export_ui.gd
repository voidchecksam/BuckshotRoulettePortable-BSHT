# Export the 2D UI: control rects, bracket textures, and focus links.
#
# scene_graph.json carries name/path/type and a 3D transform. Control nodes are
# laid out by anchors and offsets, not by a Transform3D, so it says nothing
# about where any of them is.
#
# Layout is measured, not computed: get_global_rect() is read after the tree
# has processed several frames. Anchors, offsets, containers and theme minimum
# sizes all resolve during layout; reading rects in _initialize() gives every
# node the same unresolved default. That is why this exporter runs in _process
# and counts frames rather than doing its work up front.
#
# Textures come out as raw RGBA. The .png sources on disk are the pre-import
# originals; what the game samples is the .ctex in .godot/imported, which may be
# VRAM-compressed and is not a PNG at all. Godot, which has every codec, hands
# over decompressed RGBA8, and the port freads pixels. No image decoder ships
# in the game.
#
# Run:
#   godot --headless --path <proj> --script res://tools/godot/export_ui.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")

var OUT_JSON := SceneArgs.extracted("ui_layout.json")
var OUT_PIXELS := SceneArgs.extracted("ui_textures.bin")

# The subtree that is actually the game's UI. "post processing" also lives under
# Camera but is the shader chain, which the renderer already owns.
const UI_ROOT := "Camera/dialogue UI"

# Frames to let layout settle before measuring. One is enough in practice;
# three costs nothing and removes the question.
const SETTLE_FRAMES := 3

# The Vita panel is 960x544 and the design resolution is 960x540. Nothing on
# screen can show more detail than that.
const MAX_TEXTURE_EDGE := 960

var _root: Node = null
var _frames := 0

func _initialize() -> void:
	var scene: PackedScene = load(SceneArgs.scene_path())
	if scene == null:
		print("FATAL: could not load main.tscn"); quit(1); return
	_root = scene.instantiate()
	if _root == null:
		print("FATAL: could not instantiate main.tscn"); quit(1); return
	get_root().add_child(_root)

func _process(_delta: float) -> bool:
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_export()
	quit()
	return true

func _export() -> void:
	var ui := _root.get_node_or_null(NodePath(UI_ROOT))
	if ui == null:
		print("FATAL: no node at ", UI_ROOT); quit(1); return

	var controls: Array = []
	var textures: Array = []       # metadata, parallel to the pixel pool
	var tex_ids := {}              # resource_path -> index
	var pixels := PackedByteArray()
	var counts := {}
	var unmeasured: Array = []

	_walk(ui, _root, controls, textures, tex_ids, pixels, counts, unmeasured)

	# Focus links are resolved after the walk: a neighbour may be any control in
	# the subtree, including one visited later.
	var by_path := {}
	for i in range(controls.size()):
		by_path[controls[i]["path"]] = i
	var explicit_links := 0
	var highlights := 0
	for c in controls:
		if c.has("highlight"):
			var hp: String = c["highlight"]
			c["highlight_index"] = by_path[hp] if by_path.has(hp) else -1
			if c["highlight_index"] >= 0:
				highlights += 1
		for side in ["top", "bottom", "left", "right"]:
			var key: String = "focus_" + str(side)
			var p: String = c[key]
			if p == "":
				c[key + "_index"] = -1
			elif by_path.has(p):
				c[key + "_index"] = by_path[p]
				explicit_links += 1
			else:
				c[key + "_index"] = -1

	var vp := get_root().get_visible_rect().size

	var out := {
		"design_width": int(vp.x),
		"design_height": int(vp.y),
		"controls": controls,
		"textures": textures,
		"stats": {
			"controls": controls.size(),
			"textures": textures.size(),
			"pixel_bytes": pixels.size(),
			"explicit_focus_links": explicit_links,
			"by_type": counts,
			"unmeasured": unmeasured.size(),
		},
	}

	var jf := FileAccess.open(SceneArgs.out(OUT_JSON), FileAccess.WRITE)
	if jf == null:
		print("FATAL: cannot write ", SceneArgs.out(OUT_JSON)); quit(1); return
	jf.store_string(JSON.stringify(out, "  "))
	jf.close()

	var pf := FileAccess.open(SceneArgs.out(OUT_PIXELS), FileAccess.WRITE)
	if pf == null:
		print("FATAL: cannot write ", SceneArgs.out(OUT_PIXELS)); quit(1); return
	pf.store_buffer(pixels)
	pf.close()

	print("controls: ", controls.size(), "  ", counts)
	print("textures: ", textures.size(), " unique, ",
		"%.2f MB" % (float(pixels.size()) / 1048576.0), " raw RGBA")
	print("focus: ", explicit_links, " explicit neighbour links")
	print("buttons: ", highlights, " resolved highlight targets")
	if unmeasured.size() > 0:
		# Loud: a zero-size rect draws nothing and looks like a missing feature.
		print("WARNING: ", unmeasured.size(), " controls have a zero-area rect")
		for u in unmeasured.slice(0, 10):
			print("    ", u)
	print("wrote ", SceneArgs.out(OUT_JSON), " and ", SceneArgs.out(OUT_PIXELS))

func _walk(n: Node, root: Node, controls: Array, textures: Array,
		tex_ids: Dictionary, pixels: PackedByteArray, counts: Dictionary,
		unmeasured: Array) -> void:
	if n is Control:
		var c := n as Control
		var cls := c.get_class()
		counts[cls] = counts.get(cls, 0) + 1

		var r := c.get_global_rect()
		var path := String(root.get_path_to(c))

		# visible_in_tree, not `visible`: a shown node under a hidden parent is
		# not on screen, and the port would otherwise draw the whole of every
		# scene's UI at once.
		var entry := {
			"path": path,
			"name": c.name,
			"type": cls,
			"x": r.position.x, "y": r.position.y,
			"w": r.size.x, "h": r.size.y,
			# The rect above is already scaled (get_global_rect); a Label's text is
			# laid out unscaled and scaled with it.
			"scale": [c.get_global_transform().get_scale().x, c.get_global_transform().get_scale().y],
			"visible": c.visible,
			"visible_in_tree": c.is_visible_in_tree(),
			"modulate": [c.modulate.r, c.modulate.g, c.modulate.b, c.modulate.a],
			"self_modulate": [c.self_modulate.r, c.self_modulate.g,
				c.self_modulate.b, c.self_modulate.a],
			"focus_mode": int(c.focus_mode),
			"texture": -1,
			"text": "",
			"focus_top": _neighbour(c, root, "top"),
			"focus_bottom": _neighbour(c, root, "bottom"),
			"focus_left": _neighbour(c, root, "left"),
			"focus_right": _neighbour(c, root, "right"),
		}

		if r.size.x <= 0.0 or r.size.y <= 0.0:
			unmeasured.append(path)

		# A ColorRect draws a flat colour and has no texture. Without its colour
		# the port knows the rect exists and has nothing to fill it with (the death
		# blackout and the dialogue backdrop would be invisible).
		if c is ColorRect:
			var cr := c as ColorRect
			entry["color"] = [cr.color.r, cr.color.g, cr.color.b, cr.color.a]

		if c is TextureRect:
			var tr := c as TextureRect
			entry["texture"] = _texture(tr.texture, textures, tex_ids, pixels)
			entry["stretch_mode"] = int(tr.stretch_mode)
		elif c is Label:
			_label(c as Label, entry)
		elif c is ColorRect:
			# The dialogue backdrop is a ColorRect; its colour is the whole of it.
			var cr := c as ColorRect
			entry["color"] = [cr.color.r, cr.color.g, cr.color.b, cr.color.a]
		elif c is Button:
			entry["text"] = (c as Button).text
			_button_class(c, root, entry)

		controls.append(entry)

	for child in n.get_children():
		_walk(child, root, controls, textures, tex_ids, pixels, counts, unmeasured)

func _button_class(c: Control, root: Node, entry: Dictionary) -> void:
	# The bracket highlight is not a visibility toggle. ButtonClass.gd changes
	# ui.modulate.a between two exported per-instance opacities, on a CanvasItem
	# it holds a reference to. Both the target and the two values have to come
	# out of the scene: the script's own defaults (1.0 and 0.78) are overridden
	# per button, and guessing them would give every bracket the same feel.
	for child in c.get_children():
		if not (child is Node) or child.get_script() == null:
			continue
		var sc = child.get_script()
		var name: String = ""
		if sc != null and sc.resource_path != "":
			name = sc.resource_path.get_file()
		if name != "ButtonClass.gd" and name != "ButtonClass_Main.gd":
			continue

		entry["button_class"] = name
		entry["alias"] = str(child.get("alias"))
		entry["is_active"] = bool(child.get("isActive"))
		entry["is_dynamic"] = bool(child.get("isDynamic"))
		entry["opacity_inactive"] = float(child.get("ui_opacity_inactive"))
		entry["opacity_active"] = float(child.get("ui_opacity_active"))

		# The CanvasItem whose alpha the button drives: usually its own bracket
		# TextureRect, but the scene is free to point it elsewhere.
		var target = child.get("ui")
		entry["highlight"] = String(root.get_path_to(target)) if target != null else ""

		# Some buttons highlight geometry, not a bracket. When is3D is set, SetUI
		# drives ui_3D.transparency (0 focused, 1 not) instead of a CanvasItem's
		# alpha: that is how the items on the table light up. The path is a 3D
		# node, so it joins to scene_graph.json, not here.
		entry["is_3d"] = child.get("is3D") == true  # the menu's ButtonClass has no is3D
		var target3d = child.get("ui_3D")
		entry["highlight_3d"] = String(root.get_path_to(target3d)) if target3d != null else ""
		return

func _label(l: Label, entry: Dictionary) -> void:
	# Which font a Label ends up with comes from a theme, a theme override or the
	# project default, and which one wins is not visible in the .tscn. Ask the
	# node, exactly as export_fonts.gd does, so the two agree on the key.
	entry["text"] = l.text
	entry["align_h"] = int(l.horizontal_alignment)
	entry["align_v"] = int(l.vertical_alignment)
	entry["autowrap"] = int(l.autowrap_mode)
	entry["uppercase"] = l.uppercase
	entry["clip_text"] = l.clip_text
	entry["overrun"] = int(l.text_overrun_behavior)
	# label.cpp: LabelSettings first, else the theme (3 by default; some are 0).
	entry["line_spacing"] = l.label_settings.line_spacing if l.label_settings else l.get_theme_constant("line_spacing")

	var f: Font = l.get_theme_font("font")
	var size: int = l.get_theme_font_size("font_size")
	var file := _as_font_file(f)
	var path := "(none)"
	if file != null:
		path = file.resource_path
		if path == "":
			path = "(embedded)"
	entry["font"] = "%s @ %d" % [path, size]
	entry["font_size"] = size

	var col: Color = l.get_theme_color("font_color")
	entry["font_colour"] = [col.r, col.g, col.b, col.a]

	# The game fakes a drop shadow with a second Label offset behind the first
	# (`item name_shadow`), so the theme shadow is usually off. Exported anyway:
	# absent is a fact, and guessing it is zero is not.
	var sh: Color = l.get_theme_color("font_shadow_color")
	entry["shadow_colour"] = [sh.r, sh.g, sh.b, sh.a]
	entry["shadow_offset"] = [
		l.get_theme_constant("shadow_offset_x"),
		l.get_theme_constant("shadow_offset_y"),
	]

func _as_font_file(f: Font) -> FontFile:
	if f == null:
		return null
	if f is FontFile:
		return f as FontFile
	if f is FontVariation:
		return _as_font_file((f as FontVariation).base_font)
	return null

func _neighbour(c: Control, root: Node, side: String) -> String:
	# focus_neighbor_* is a NodePath relative to the control. Empty means Godot
	# picks a neighbour geometrically at runtime, which the port reproduces.
	var np: NodePath = c.get("focus_neighbor_" + side)
	if np == null or np.is_empty():
		return ""
	var target := c.get_node_or_null(np)
	if target == null:
		return ""
	return String(root.get_path_to(target))

func _texture(tex: Texture2D, textures: Array, tex_ids: Dictionary,
		pixels: PackedByteArray) -> int:
	if tex == null:
		return -1
	var key: String = tex.resource_path
	if key == "":
		# An embedded texture with no path cannot be deduplicated by path, so
		# key it by identity instead of silently merging distinct images.
		key = "rid:%d" % tex.get_rid().get_id()
	if tex_ids.has(key):
		return tex_ids[key]

	var img: Image = tex.get_image()
	if img == null:
		return -1
	img = img.duplicate()
	if img.is_compressed():
		# VRAM-compressed source; decompress so the port never needs a codec.
		if img.decompress() != OK:
			print("WARNING: could not decompress ", key)
			return -1
	img.convert(Image.FORMAT_RGBA8)
	# get_data() returns every mipmap level concatenated (the 104x104 bracket's
	# 10,816 pixels come back as 14,411). The UI is drawn at 1:1 and samples no
	# mip, so the extra levels are both wrong for the port and a third of the
	# bytes.
	img.clear_mipmaps()

	# A texture larger than the screen costs memory and buys nothing. The pills
	# overlay is authored at 1920x1080 and is most of this pool on its own; at
	# 960 wide it is a quarter of the size and identical on a 960x544 panel.
	# Only ever downscales: a small bracket is never blown up and blurred.
	var scaled := ""
	var w := img.get_width()
	var h := img.get_height()
	if w > MAX_TEXTURE_EDGE or h > MAX_TEXTURE_EDGE:
		var f: float = float(MAX_TEXTURE_EDGE) / float(max(w, h))
		var nw: int = max(1, int(round(float(w) * f)))
		var nh: int = max(1, int(round(float(h) * f)))
		img.resize(nw, nh, Image.INTERPOLATE_LANCZOS)
		scaled = " (from %dx%d)" % [w, h]

	var data := img.get_data()
	var index := textures.size()
	print("  texture %d: %dx%d %.2f MB%s  %s" % [index, img.get_width(),
		img.get_height(), float(data.size()) / 1048576.0, scaled, key])
	textures.append({
		"key": key,
		"width": img.get_width(),
		"height": img.get_height(),
		"offset": pixels.size(),
		"bytes": data.size(),
	})
	pixels.append_array(data)
	tex_ids[key] = index
	return index
