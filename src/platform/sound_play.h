// One play of a banked sound, as Godot starts it: what the audio sink asks
// platform::Audio for. Plain data, no SDL, so app/ can fill it.
#pragma once

namespace platform {

struct SoundPlay {
    const char* name = nullptr;     // the bank's key, "temp gunshot_live.wav"
    float gainL = 1.0f;             // Godot's mix gain per channel (godot::OutputGain)
    float gainR = 1.0f;
    float pitch = 1.0f;             // AudioStreamPlayer.pitch_scale
    float fromSeconds = 0.0f;       // play(from) / an audio key's start_offset
    float endOffsetSeconds = 0.0f;  // an audio key's end_offset: released this far before the end
    bool rampIn = true;             // the AudioServer's start ramp (not for a polyphonic add)
    bool positional = false;        // a 3D player: its attenuation shelf always runs
    float shelf = 1.0f;             // that shelf's gain (1: flat)
    float shelfCutoffHz = 5000.0f;
};

}  // namespace platform
