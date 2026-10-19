# Export round definitions and item pool weights from main.tscn.
#
# RoundClass and AmountResource are @export'ed Resources, so their values live
# as sub-resources inside scenes/main.tscn, not in any .gd file: the scripts
# alone hold none of the game's balance.
#
# Run:
#   godot --headless --path <proj> --script res://tools/godot/export_round_table.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")
var OUT := SceneArgs.extracted("round_table.json")

func _initialize() -> void:
	print("loading main.tscn (137 MB, this takes a moment)...")
	var scene: PackedScene = load("res://scenes/main.tscn")
	if scene == null:
		printerr("FAIL: cannot load main.tscn")
		quit(1); return
	var root := scene.instantiate()
	if root == null:
		printerr("FAIL: cannot instantiate main.tscn")
		quit(1); return

	var rounds: Array = []
	var batches: Array = []
	var items: Array = []
	var seen_rounds := {}
	_walk(root, root, rounds, batches, items, seen_rounds)

	var data := {"rounds": rounds, "batches": batches, "items": items}
	var abs := ProjectSettings.globalize_path(OUT)
	DirAccess.make_dir_recursive_absolute(abs.get_base_dir())
	var f := FileAccess.open(abs, FileAccess.WRITE)
	if f == null:
		printerr("FAIL: cannot write ", abs)
		quit(1); return
	f.store_string(JSON.stringify(data, "  "))
	f.close()

	print("rounds=", rounds.size(), " batches=", batches.size(), " items=", items.size())
	for r in rounds:
		print("  round ", r["roundIndex"],
			  ": hp=", r["startingHealth"],
			  " live=", r["amountLive"],
			  " blank=", r["amountBlank"],
			  " items=", r["numberOfItemsToGrab"])
	print("wrote ", abs)
	root.free()
	quit(0)

func _walk(n: Node, root: Node, rounds: Array, batches: Array,
		   items: Array, seen: Dictionary) -> void:
	# RoundBatch nodes carry the ordered round list for a batch.
	var ra = n.get("roundArray")
	var bi = n.get("batchIndex")
	if ra != null and bi != null:
		var idxs: Array = []
		for r in ra:
			if r == null:
				continue
			var key := str(r.get_instance_id())
			idxs.append(int(r.roundIndex))
			if not seen.has(key):
				seen[key] = true
				rounds.append(_round_to_dict(r))
		batches.append({"batchIndex": int(bi), "roundIndices": idxs})

	# The Amounts node carries item pool weights.
	var aa = n.get("array_amounts")
	if aa != null:
		for a in aa:
			if a == null:
				continue
			items.append({
				"itemName": String(a.itemName),
				"amount_active": int(a.amount_active),
				"amount_main": int(a.amount_main),
				"amount_don": int(a.amount_don),
			})

	for c in n.get_children():
		_walk(c, root, rounds, batches, items, seen)

func _round_to_dict(r) -> Dictionary:
	return {
		"roundIndex": int(r.roundIndex),
		"startingHealth": int(r.startingHealth),
		"amountLive": int(r.amountLive),
		"amountBlank": int(r.amountBlank),
		"usingItems": bool(r.usingItems),
		"shufflingArray": bool(r.shufflingArray),
		"insertingInRandomOrder": bool(r.insertingInRandomOrder),
		"numberOfItemsToGrab": int(r.numberOfItemsToGrab),
		"showingIndicator": bool(r.showingIndicator),
		"indicatorNumber": int(r.indicatorNumber),
		"sequenceHidden": bool(r.sequenceHidden),
		"bootingUpCounter": bool(r.bootingUpCounter),
		"isFirstRound": bool(r.isFirstRound),
		"hasIntroductoryText": bool(r.hasIntroductoryText),
		"hasIntro2": bool(r.hasIntro2),
	}
