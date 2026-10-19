// The sound bank: short effects decoded offline, music streamed on device.
//
// Decoding every .ogg in the game to PCM costs hundreds of MB, and the Vita has
// roughly 69 MB of RAM free, so a bank of everything is not an option. By
// duration the library splits cleanly: the short effects and cues are decoded
// offline into this bank (IMA ADPCM, at their native rate, stereo where heard)
// and live in RAM, where a gunshot must start on the exact frame it is asked
// for. Music and ambience ship as .ogg and stream with stb_vorbis, a chunk at
// a time.
//
// The device mixes at 48 kHz stereo and resamples each sound from its own rate
// with Godot's cubic (core/godot_mix.h).
//
// Headless: no GL, no SDL, so the bank format is testable without a device.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

// v3: each sound at its source's own rate (44.1 or 48 kHz), stereo where the
// source is audibly stereo (checked against Godot's own playback with
// tools/compare_cues.py). v2 was mono IMA ADPCM at 44.1 kHz; v1 16-bit at
// 22.05 kHz.
// v4: a per-sound flag: a .wav plays with linear interpolation, as Godot's
// AudioStreamPlaybackWAV does; an .ogg with the resampler's cubic.
inline constexpr uint32_t kSoundBankVersion = 4;

// A stereo source is kept stereo when its side signal (L - R) / 2 is within
// this many dB of its mid (L + R) / 2; below it the difference is inaudible
// and the sound is stored once.
inline constexpr double kStereoSideThresholdDb = -40.0;

enum class SoundBankError {
    None = 0,
    FileNotFound,
    TooSmall,
    BadMagic,
    BadVersion,
    Truncated,
    Inconsistent,
};

const char* SoundBankErrorText(SoundBankError e);

// One channel's ADPCM run in the shared pool.
struct SoundChannel {
    uint32_t offset = 0;      // BYTE offset into the pool ((frames + 1) / 2 bytes)
    int16_t  predictor = 0;   // the ADPCM decoder's starting state
    uint8_t  stepIndex = 0;
};

struct Sound {
    std::string  name;         // "dealer use handsaw.ogg", as animation refers to it
    uint32_t     frames = 0;   // per channel
    uint32_t     rate = 44100; // the source's own sample rate
    int          channels = 1; // 2: channel[1] is the right
    bool         linear = false; // a .wav: linear interpolation (Godot's WAV playback)
    SoundChannel channel[2];

    float Seconds() const { return static_cast<float>(frames) / static_cast<float>(rate); }
};

// True when keeping both channels is audible: the side within
// kStereoSideThresholdDb of the mid.
bool IsAudiblyStereo(const std::vector<int16_t>& left, const std::vector<int16_t>& right);

class SoundBank {
public:
    bool LoadFromFile(const char* path);
    // Why the last LoadFromFile failed (SoundBankErrorText names it), so the
    // device report can say "wrong bank version; re-bake" rather than
    // "missing".
    SoundBankError LastError() const { return lastError_; }

    // -1 when absent, so a missing cue reads as "no sound called X" rather than
    // as silence nobody can explain.
    int Find(const char* name) const;

    const Sound& At(int i) const { return sounds_[static_cast<std::size_t>(i)]; }
    std::size_t Count() const { return sounds_.size(); }
    const std::vector<Sound>& Sounds() const { return sounds_; }

    // The shared ADPCM pool every Sound indexes into (two samples a byte).
    const uint8_t* Data() const { return data_.data(); }
    std::size_t DataSize() const { return data_.size(); }

    std::size_t BytesUsed() const { return data_.size(); }

    void Clear() {
        sounds_.clear();
        data_.clear();
    }

    // Used by the baker: 16-bit PCM at `rate`, encoded here. `right` empty: a
    // mono sound. The runtime only reads.
    void AddForBake(const std::string& name, const std::vector<int16_t>& left, const std::vector<int16_t>& right,
                    uint32_t rate, bool linear);
    bool WriteToFile(const char* path) const;

private:
    std::vector<Sound>   sounds_;
    std::vector<uint8_t> data_;
    SoundBankError       lastError_ = SoundBankError::None;
};

}  // namespace assets
