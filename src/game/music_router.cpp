#include "game/music_router.h"

#include "godot/node.h"

#include <cmath>
#include <string_view>

namespace game {
namespace {

// One audible step: a fade reports once a frame at most.
bool Moved(const godot::StereoGain& a, const godot::StereoGain& b) {
    return std::fabs(a.l - b.l) > 0.002f || std::fabs(a.r - b.r) > 0.002f;
}

}  // namespace

void MusicRouter::Bind(godot::AudioPlayback* player) {
    player_ = player;
    playing_.clear();
    gain_ = {};
    sentCutoff_ = -1.0f;
    adopt_.clear();
}

namespace {

std::string_view FileName(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\:");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

}  // namespace

std::string MusicRouter::PathFor(const std::string& stream) const {
    std::string path = dataRoot_;
    if (!path.empty() && path.back() != '/') path += '/';
    if (!subDir_.empty()) {
        path += subDir_;
        if (path.back() != '/') path += '/';
    }
    std::string name = stream;
    if (spaceFree_) {
        for (char& ch : name) {
            if (ch == ' ') ch = '_';
        }
    }
    return path + name;
}

MusicRouter::Change MusicRouter::Poll() {
    Change c;
    if (!player_) return c;

    const bool playing = player_->is_playing();
    const std::string& stream = player_->get_stream();

    if (!playing || stream.empty()) {
        if (!playing_.empty()) {
            playing_.clear();
            c.kind = Kind::Stop;
        }
        return c;
    }

    const float cutoff = cutoff_ ? static_cast<float>(*cutoff_) : 0.0f;

    if (stream != playing_) {
        playing_ = stream;
        gain_ = godot::OutputGain(*player_);
        sentCutoff_ = cutoff;
        c.path = PathFor(stream);
        c.kind = !adopt_.empty() && FileName(adopt_) == FileName(c.path) ? Kind::Adopt : Kind::Play;
        adopt_.clear();
        c.gain = gain_;
        c.cutoffHz = cutoff;
        return c;
    }

    // A pan moves the cutoff every frame; an audible step is about a semitone.
    if (cutoff_ && std::fabs(cutoff - sentCutoff_) > sentCutoff_ * 0.02f + 1.0f) {
        sentCutoff_ = cutoff;
        c.kind = Kind::Cutoff;
        c.cutoffHz = cutoff;
        return c;
    }

    const godot::StereoGain gain = godot::OutputGain(*player_);
    // A fade moves volume_db every frame; report only audible steps, so a
    // ramp costs one call per frame rather than one per float wobble.
    if (Moved(gain, gain_)) {
        gain_ = gain;
        c.kind = Kind::Gain;
        c.gain = gain;
    }
    return c;
}

}  // namespace game
