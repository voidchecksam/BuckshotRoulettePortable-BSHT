// Each audio stream's length and loop flag as Godot imported it:
// tools/godot/export_audio_streams.gd -> assets/extracted/audio_streams.json.
// The audio sink reports the length so a player's is_playing lasts as long as
// its sound, and a looping stream (ambience) never ends on its own.
#pragma once

#include <string>
#include <unordered_map>

namespace assets {

struct AudioStreamInfo {
    float length = 0.0f;   // seconds, AudioStream.get_length()
    bool  loop = false;    // AudioStreamOggVorbis.loop (WAV: loop_mode != disabled)
};

class AudioStreams {
public:
    // {"streams":[{"name":"x.ogg","length":1.5,"loop":false}, ...]}. False (and
    // empty) when there is no "streams" array.
    bool Parse(const std::string& json);
    bool LoadFromFile(const char* path);
    // By the stream's file name, as AudioPlayback::get_stream() holds it.
    const AudioStreamInfo* Find(const std::string& name) const;
    std::size_t Size() const { return byName_.size(); }

private:
    std::unordered_map<std::string, AudioStreamInfo> byName_;
};

}  // namespace assets
