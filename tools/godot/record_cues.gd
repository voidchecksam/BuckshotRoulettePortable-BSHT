# Records how Godot 4.1.4 itself plays each sound: one cue at a time, captured
# from the Master bus by an AudioEffectRecord, one WAV per cue, for
# tools/compare_cues.py against the port's tools/render_cues.cpp.
#
#   plain:<cue>  an AudioStreamPlayer at 0 dB (gain 1): the codec and mixer alone
#   2d:<cue>     an AudioStreamPlayer2D at the canvas origin, panning_strength 0,
#                as most of main.tscn's players are (expected 0.3623 a side)
#   3d:<cue>     an AudioStreamPlayer3D 3 units in front of the current camera,
#                volume -12.124, panning_strength 0.57: the health counter's
#                round-wave speaker (expected 0.5836 a side)
#
# Plays in real time on the audio thread. Run it through the private-user://
# wrapper, with the dummy audio driver and absolute paths:
#   tools/godot/run_private.sh res://tools/godot/record_cues.gd --audio-driver Dummy -- <cues.txt> <out dir>
extends SceneTree

const TAIL_MS := 250

var _jobs: Array[String] = []
var _out_dir := ""
var _paths := {}
var _record: AudioEffectRecord
var _players := {}
var _job := -1
var _phase := 0          # 0 start next, 1 playing, 2 tail
var _tail_until := 0
var _current: Node


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		printerr("usage: -- <cues.txt> <out dir>")
		quit(2)
		return
	_out_dir = args[1]
	DirAccess.make_dir_recursive_absolute(_out_dir)
	_index_audio("res://audio")
	var names := FileAccess.get_file_as_string(args[0]).split("\n", false)
	for n in names:
		if n.strip_edges() != "":
			_jobs.append("plain:" + n.strip_edges())
	for n in ["temp gunshot_live.wav", "round blinker wave.ogg"]:
		_jobs.append("2d:" + n)
		_jobs.append("3d:" + n)

	_record = AudioEffectRecord.new()
	_record.format = AudioStreamWAV.FORMAT_16_BITS
	AudioServer.add_bus_effect(0, _record)

	var world := Node3D.new()
	root.add_child(world)
	var cam := Camera3D.new()
	world.add_child(cam)
	cam.current = true
	var p3 := AudioStreamPlayer3D.new()
	p3.position = Vector3(0, 0, -3)
	p3.volume_db = -12.124
	p3.panning_strength = 0.57
	world.add_child(p3)
	var p2 := AudioStreamPlayer2D.new()
	p2.panning_strength = 0.0
	root.add_child(p2)
	var p := AudioStreamPlayer.new()
	root.add_child(p)
	_players = {"plain": p, "2d": p2, "3d": p3}
	print("record_cues: ", _jobs.size(), " jobs, mix rate ", AudioServer.get_mix_rate())


func _index_audio(dir: String) -> void:
	for f in DirAccess.get_files_at(dir):
		if f.ends_with(".ogg") or f.ends_with(".wav"):
			_paths[f] = dir + "/" + f
	for d in DirAccess.get_directories_at(dir):
		_index_audio(dir + "/" + d)


func _process(_delta: float) -> bool:
	match _phase:
		0:
			_job += 1
			if _job >= _jobs.size():
				print("record_cues: done")
				quit(0)
				return true
			var kind := _jobs[_job].get_slice(":", 0)
			var cue := _jobs[_job].substr(kind.length() + 1)
			if not _paths.has(cue):
				printerr("record_cues: no file for ", cue)
				return false
			_current = _players[kind]
			_current.stream = load(_paths[cue])
			_record.set_recording_active(true)
			_current.play()
			_phase = 1
		1:
			if not _current.playing:
				_tail_until = Time.get_ticks_msec() + TAIL_MS
				_phase = 2
		2:
			if Time.get_ticks_msec() >= _tail_until:
				_record.set_recording_active(false)
				var wav := _record.get_recording()
				var file := _jobs[_job].replace(":", "__") + ".wav"
				if wav == null or wav.save_to_wav(_out_dir + "/" + file) != OK:
					printerr("record_cues: could not save ", file)
				_phase = 0
	return false
