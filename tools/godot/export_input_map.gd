# project.godot [input] and [display] mouse_cursor -> assets/extracted/input_map.json.
#
# Only the event fields the port matches on (core/input/input_event.cpp
# action_match at 4.1.4-stable): keycode / physical_keycode for keys,
# button_index for joypad and mouse buttons. Actions keep the project's order.
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")

func _initialize() -> void:
	var out := {"format": 1, "actions": [], "cursor": {}}
	var skipped := 0
	for prop in ProjectSettings.get_property_list():
		var name: String = prop["name"]
		if not name.begins_with("input/"):
			continue
		var v = ProjectSettings.get_setting(name)
		var events: Array = []
		for ev in v["events"]:
			if ev is InputEventKey:
				events.append({"type": "key", "device": ev.device, "keycode": ev.keycode, "physical_keycode": ev.physical_keycode})
			elif ev is InputEventJoypadButton:
				events.append({"type": "joypad_button", "device": ev.device, "button_index": ev.button_index})
			elif ev is InputEventMouseButton:
				events.append({"type": "mouse_button", "device": ev.device, "button_index": ev.button_index})
			else:
				skipped += 1
		out["actions"].append({"name": name.substr(6), "deadzone": v["deadzone"], "events": events})
	var img: String = ProjectSettings.get_setting("display/mouse_cursor/custom_image", "")
	var hot: Vector2 = ProjectSettings.get_setting("display/mouse_cursor/custom_image_hotspot", Vector2())
	out["cursor"] = {"image": img.get_file(), "hotspot": [hot.x, hot.y]}
	var path := ProjectSettings.globalize_path(SceneArgs.extracted("input_map.json"))
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(JSON.stringify(out, "  "))
	f.close()
	print("input_map: %d actions, %d unsupported events skipped, cursor %s" % [out["actions"].size(), skipped, img.get_file()])
	quit()
