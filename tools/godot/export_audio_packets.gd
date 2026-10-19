# The game's sounds as files the tool can rebuild. A shipped game keeps Ogg
# Vorbis as Godot's packet sequence, not as .ogg files: each one is written as
# its pages of packets (<name>.oggpk). WAV samples are saved as .wav. Output:
# <out>/audio/ and <out>/audio/music/, mirroring res://audio.
extends SceneTree

const SceneArgs := preload("res://tools/godot/scene_args.gd")

func _init() -> void:
	var total := 0
	for sub in ["", "music"]:
		var n := _export_dir("res://audio".path_join(sub), SceneArgs.extracted("audio").path_join(sub))
		if n < 0:
			quit(1)
			return
		total += n
	print("audio: ", total, " sounds")
	quit(0 if total > 0 else 1)

# The sounds in one folder, or -1 if one cannot be written.
func _export_dir(dir: String, out_dir: String) -> int:
	DirAccess.make_dir_recursive_absolute(out_dir)
	# A shipped game lists only "x.ogg.import"; an editor project lists both.
	var names := {}
	for f in DirAccess.get_files_at(dir):
		var name := f.trim_suffix(".import")
		if name.get_extension() in ["ogg", "wav"]:
			names[name] = true
	var count := 0
	for name in names:
		var stream = load(dir.path_join(name))
		var out := out_dir.path_join(name)
		if stream is AudioStreamOggVorbis:
			if not _write_packets(stream.packet_sequence, out + "pk"):
				return -1
		elif stream is AudioStreamWAV:
			if stream.save_to_wav(out) != OK:
				printerr("FATAL: cannot write ", out)
				return -1
		else:
			# A sound that does not load would be missing from the bank unnoticed.
			printerr("FATAL: cannot read the sound ", dir.path_join(name))
			return -1
		count += 1
	return count

# BSPK v1, little-endian: f32 rate, u32 pages, then per page i64 granule,
# u32 packets and per packet u32 size + bytes. src/extract/ogg_rebuild.cpp reads it.
func _write_packets(seq: OggPacketSequence, path: String) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		printerr("FATAL: cannot write ", path)
		return false
	f.store_buffer("BSPK".to_ascii_buffer())
	f.store_32(1)
	f.store_float(seq.sampling_rate)
	var pages: Array = seq.packet_data
	var granules: PackedInt64Array = seq.granule_positions
	f.store_32(pages.size())
	for i in pages.size():
		f.store_64(granules[i])
		var packets: Array = pages[i]
		f.store_32(packets.size())
		for p in packets:
			f.store_32(p.size())
			f.store_buffer(p)
	return true
