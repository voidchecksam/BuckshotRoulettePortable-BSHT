// The seam between MusicManager's AudioStreamPlayer and the platform's music
// streamer.
//
// Other sounds go through the tree's audio sink into the resident ADPCM bank.
// Music cannot: the tracks are minutes long and would not fit in RAM decoded,
// so they stream from disk (platform::Audio::PlayMusic). The sink only fires on
// play(), while SpeakerController fades volume_db over many frames (the
// briefcase ducking in ItemManager.GrabSpook is that), so the streamer has to
// be told about every change too.
//
// Headless: this is the arithmetic (which file, what gain, still playing?),
// and app::GameApp does the calling.
#pragma once

#include "godot/audio_spatial.h"

#include <string>

namespace godot {
class AudioPlayback;
}

namespace game {

class MusicRouter {
public:
    // `dataRoot` is where the game's files live; `subDir` is the music folder
    // under it ("" in bsht-extract's game folder, which is flat).
    MusicRouter() = default;
    MusicRouter(std::string dataRoot, std::string subDir)
        : dataRoot_(std::move(dataRoot)), subDir_(std::move(subDir)) {}

    // The MusicManager's speaker_music. Null unbinds.
    void Bind(godot::AudioPlayback* player);

    bool Bound() const { return player_ != nullptr; }
    // True for the music player itself, so the audio sink can let it past.
    bool IsMusic(const godot::AudioPlayback* p) const { return p != nullptr && p == player_; }

    // What the audio sink must report as the music stream's length. A track
    // loops until something calls stop(), but AudioPlayback::tick stops a
    // player once its reported length runs out, and with the SFX default of 0
    // the music would stop one frame after MusicManager pressed play. Longer
    // than any session.
    static constexpr float kNeverEnds = 1.0e9f;
    float OnPlay() const { return kNeverEnds; }

    // FilterController's effect_lowPass_cutoff_hz: the music bus's low-pass,
    // panned between the track's default (156 Hz, muffled at the table) and
    // lowPassMaxValue (the lobby). Null unbinds; 0 means no filtering.
    void BindFilter(const double* cutoffHz) { cutoff_ = cutoffHz; }

    // bsht-extract's game folder names the tracks with underscores for
    // spaces.
    void SetSpaceFreeNames(bool on) { spaceFree_ = on; }

    // The platform already streams `path` (the loading screen started it): if
    // the player's first track is the same file, that Play reports Adopt and
    // the stream goes on. Files match by name, as the device renames them.
    // Call after Bind; Bind forgets it.
    void AdoptRunning(const std::string& path) { adopt_ = path; }

    // Adopt: a Play whose stream is already running (path, gain, cutoff set).
    enum class Kind { None, Play, Stop, Gain, Cutoff, Adopt };
    struct Change {
        Kind kind = Kind::None;
        std::string path;      // Play and Adopt
        godot::StereoGain gain;  // Play, Adopt and Gain: Godot's mix gain (godot::OutputGain)
        float cutoffHz = 0.0f; // Play, Adopt and Cutoff
    };

    // What the streamer should do since the last call. One change per call:
    // a new track reports Play (its gain included), so a fade that starts in
    // the same frame is picked up by the next poll.
    Change Poll();

private:
    std::string PathFor(const std::string& stream) const;

    std::string dataRoot_;
    std::string subDir_;
    godot::AudioPlayback* player_ = nullptr;
    const double* cutoff_ = nullptr;
    bool spaceFree_ = false;
    std::string adopt_;        // the running file the first Play may adopt
    float sentCutoff_ = -1.0f;
    std::string playing_;      // the stream the platform is streaming now
    godot::StereoGain gain_;   // the gain it was last told
};

}  // namespace game
