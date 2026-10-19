# Each audio stream's length and loop flag, as Godot imported it. The port's
# audio sink reports the length, so a player's is_playing lasts as long as its
# sound, and a looping ambience never ends on its own.
#
# Loads resources only; no scene is instantiated. Run through the wrapper:
#   tools/godot/run_private.sh res://tools/godot/export_audio_streams.gd
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")
var OUT := SceneArgs.extracted("audio_streams.json")
const DIRS := ["res://audio", "res://audio/music"]

func _initialize() -> void:
	var streams: Array = []
	for dir in DIRS:
		if not DirAccess.dir_exists_absolute(dir):
			print("WARNING: no ", dir)
			continue
		# A shipped game lists only "x.ogg.import"; an editor project lists both.
		var names := {}
		for f in DirAccess.get_files_at(dir):
			var name := String(f).trim_suffix(".import")
			if name.get_extension() in ["ogg", "wav"]:
				names[name] = true
		var sorted := names.keys()
		sorted.sort()
		for f in sorted:
			var s: AudioStream = load(dir + "/" + f)
			if s == null:
				print("WARNING: could not load ", f)
				continue
			var loop := false
			if s is AudioStreamOggVorbis:
				loop = (s as AudioStreamOggVorbis).loop
			elif s is AudioStreamWAV:
				loop = (s as AudioStreamWAV).loop_mode != AudioStreamWAV.LOOP_DISABLED
			streams.append({"name": f, "length": s.get_length(), "loop": loop})
	if streams.is_empty():
		print("FATAL: no audio streams found in ", DIRS)
		quit(1)
		return
	var out := FileAccess.open(OUT, FileAccess.WRITE)
	if out == null:
		print("FATAL: cannot write ", OUT)
		quit(1)
		return
	out.store_string(JSON.stringify({"format": 1, "streams": streams}, " "))
	out.close()
	var loops := 0
	for s in streams:
		if s["loop"]:
			loops += 1
	print(streams.size(), " streams, ", loops, " looping -> ", OUT)
	quit()
