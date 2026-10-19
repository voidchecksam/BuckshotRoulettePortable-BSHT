// The seam between every streamed AudioStreamPlayer and the platform's stream
// voices. The sound bank holds only effects of 6 s or less; a player of a
// longer file (ambience, the CRT boot loop, the death and win themes, heaven's
// music) is handed here by the audio sink, and Poll() says what the voices
// should do since the last frame. MusicManager's player keeps its own router
// (music_router.h): the music voice carries the low-pass, these are dry.
#pragma once

#include "godot/audio_spatial.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace assets {
class AudioStreams;
}
namespace godot {
class AudioPlayback;
}

namespace game {

class StreamRouter {
public:
    // Voices for streams besides the music (the platform has kVoices + 1).
    static constexpr int kVoices = 3;
    // What the sink reports for a looping stream: it never ends on its own.
    static constexpr float kNeverEnds = 1.0e9f;

    using PathFor = std::function<std::string(const std::string& stream)>;

    StreamRouter() = default;
    // `info` (lengths and loop flags) must outlive the router.
    StreamRouter(const assets::AudioStreams* info, PathFor pathFor)
        : info_(info), pathFor_(std::move(pathFor)) {}

    // The sink, for a player whose stream the bank does not hold: tracks it
    // and returns the length to report (kNeverEnds if it loops, 0 unknown).
    float OnPlay(const godot::AudioPlayback* player);

    enum class Kind { Start, Stop, Gain };
    struct Change {
        Kind kind = Kind::Start;
        int voice = 0;          // 0 .. kVoices-1
        std::string path;       // Start
        bool loop = false;      // Start
        godot::StereoGain gain; // Start, Gain: Godot's mix gain (godot::OutputGain)
        float pitch = 1.0f;     // Start: the player's pitch_scale
        bool musicBus = false;  // Start: on the "music" bus, so the table's low-pass applies
    };
    // What changed since the last call, in order.
    std::vector<Change> Poll();

    // A scene change: every voice stops at the next Poll.
    void Reset();
    // Forgets players `alive` rejects (freed nodes) without reading them.
    void Prune(const std::function<bool(const godot::AudioPlayback*)>& alive);

private:
    struct Entry {
        const godot::AudioPlayback* player = nullptr;   // null: voice idle
        std::string stream;
        godot::StereoGain gain;
        unsigned long long started = 0;           // for taking over the oldest
        uint32_t stops = 0;                       // the player's stop count at play
        bool loop = false;
        // Idle, but the platform may still be playing a ran-out one-shot's
        // tail on it: the last voice an idle pick takes (0: truly idle).
        unsigned long long tail = 0;
        bool restart = false;
        bool announced = false;
    };

    int VoiceFor(const godot::AudioPlayback* player);

    const assets::AudioStreams* info_ = nullptr;
    PathFor pathFor_;
    Entry voices_[kVoices];
    std::vector<int> pendingStops_;
    unsigned long long clock_ = 0;
};

}  // namespace game
