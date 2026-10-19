#include "game/stream_router.h"

#include "assets/audio_streams.h"
#include "godot/node.h"

#include <cmath>

namespace game {

namespace {

// One audible step: a fade or a moving camera reports once a frame at most.
bool Moved(const godot::StereoGain& a, const godot::StereoGain& b) {
    return std::fabs(a.l - b.l) > 0.002f || std::fabs(a.r - b.r) > 0.002f;
}

}  // namespace

int StreamRouter::VoiceFor(const godot::AudioPlayback* player) {
    for (int i = 0; i < kVoices; ++i) {
        if (voices_[i].player == player) return i;
    }
    return -1;
}

float StreamRouter::OnPlay(const godot::AudioPlayback* player) {
    const assets::AudioStreamInfo* info = info_ ? info_->Find(player->get_stream()) : nullptr;
    if (!info) return 0.0f;
    int v = VoiceFor(player);
    if (v < 0) {
        // A truly idle voice, else the idle one whose tail ran out longest
        // ago, else the one started longest ago is taken over.
        for (int i = 0; i < kVoices && v < 0; ++i) {
            if (!voices_[i].player && voices_[i].tail == 0) v = i;
        }
        for (int i = 0; i < kVoices; ++i) {
            if (voices_[i].player || voices_[i].tail == 0) continue;
            if (v < 0 || (voices_[v].tail != 0 && voices_[i].tail < voices_[v].tail)) v = i;
        }
        if (v < 0) {
            v = 0;
            for (int i = 1; i < kVoices; ++i) {
                if (voices_[i].started < voices_[v].started) v = i;
            }
        }
    }
    Entry& e = voices_[v];
    e.player = player;
    e.stream = player->get_stream();
    e.started = ++clock_;
    e.tail = 0;
    e.stops = player->get_stop_count();
    e.loop = info->loop;
    e.restart = true;
    e.announced = false;
    // At its pitch_scale the stream plays out in length / pitch.
    const float pitch = player->get_pitch_scale() > 0.01f ? player->get_pitch_scale() : 0.01f;
    return info->loop ? kNeverEnds : info->length / pitch;
}

std::vector<StreamRouter::Change> StreamRouter::Poll() {
    std::vector<Change> out;
    for (int v : pendingStops_) out.push_back(Change{Kind::Stop, v, {}, false, {0.0f, 0.0f}});
    pendingStops_.clear();
    for (int i = 0; i < kVoices; ++i) {
        Entry& e = voices_[i];
        if (!e.player) continue;
        if (e.restart) {
            // Started this frame (or again): play from the top.
            if (!e.player->is_playing()) {
                e = Entry{};
                continue;
            }
            const assets::AudioStreamInfo* info = info_->Find(e.stream);
            e.gain = godot::OutputGain(*e.player);
            out.push_back(Change{Kind::Start, i, pathFor_ ? pathFor_(e.stream) : e.stream,
                                 info && info->loop, e.gain, e.player->get_pitch_scale(),
                                 e.player->get_bus() == "music"});
            e.restart = false;
            e.announced = true;
            continue;
        }
        if (!e.player->is_playing() || e.player->get_stream() != e.stream) {
            // A one-shot that ran out keeps playing to the file's end: the
            // platform started it a load after play(), and Godot's length is
            // counted from play(), so a Stop would cut its tail.
            const bool ranOut = !e.loop && e.player->get_stream() == e.stream &&
                                e.player->get_stop_count() == e.stops;
            if (e.announced && !ranOut) out.push_back(Change{Kind::Stop, i, {}, false, {0.0f, 0.0f}});
            e = Entry{};
            if (ranOut) e.tail = ++clock_;
            continue;
        }
        // A 3D player's gain follows the camera as well as its volume.
        const godot::StereoGain gain = godot::OutputGain(*e.player);
        if (Moved(gain, e.gain)) {
            e.gain = gain;
            out.push_back(Change{Kind::Gain, i, {}, false, gain});
        }
    }
    return out;
}

void StreamRouter::Reset() {
    for (int i = 0; i < kVoices; ++i) {
        // A ran-out tail too: Godot frees its player with the scene.
        if ((voices_[i].player && voices_[i].announced) || voices_[i].tail != 0) pendingStops_.push_back(i);
        voices_[i] = Entry{};
    }
}

void StreamRouter::Prune(const std::function<bool(const godot::AudioPlayback*)>& alive) {
    for (int i = 0; i < kVoices; ++i) {
        if (voices_[i].player && !alive(voices_[i].player)) {
            if (voices_[i].announced) pendingStops_.push_back(i);
            voices_[i] = Entry{};
        }
    }
}

}  // namespace game
