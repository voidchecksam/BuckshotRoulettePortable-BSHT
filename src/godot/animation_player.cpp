#include "godot/animation_player.h"
#include "godot/skeleton.h"

#include <algorithm>
#include "godot/control.h"

#include "godot/particles.h"

#include "assets/node_path.h"
#include "godot/scene_tree.h"

#include <cmath>
#include <cstdlib>

namespace godot {
namespace {

using assets::Track;
using assets::TrackType;
using assets::Value;

constexpr int kLoopLinear = 1;

bool StartsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool EndsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string FileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

void AnimationPlayer::set_animations(const assets::AnimationPlayerData* data) {
    data_ = data;
    const std::size_t count = data ? data->animations.size() : 0;
    bound_.assign(count, {});
    bound_ready_.assign(count, 0u);
    current_ = assigned_.empty() ? -1 : FindAnimation(assigned_);
    if (playing_ && current_ < 0) playing_ = false;
    set_process_internal(playing_);
}

void AnimationPlayer::set_initial_state(std::string assigned, bool playing, double position,
                                        float speed_scale, std::string autoplay) {
    assigned_ = std::move(assigned);
    playing_ = playing;
    pos_ = position;
    speed_scale_ = speed_scale;
    autoplay_ = std::move(autoplay);
    has_played_ = playing;
    current_ = (data_ && !assigned_.empty()) ? FindAnimation(assigned_) : -1;
    set_process_internal(playing_);
}

int AnimationPlayer::FindAnimation(const std::string& name) const {
    if (!data_) return -1;
    for (std::size_t i = 0; i < data_->animations.size(); ++i) {
        if (data_->animations[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void AnimationPlayer::Unresolved(const std::string& what) {
    unresolved_paths_.push_back(get_path() + ": " + what);
}

// The property table: what a VALUE track may write, and the node type that
// owns it. Anything else is named as unsupported.
AnimationPlayer::BoundTrack AnimationPlayer::BindTrack(const Track& track) {
    BoundTrack b;
    const std::string path = assets::ResolveNodePath(data_->root, track.nodePath);
    b.node = get_tree() ? get_tree()->get_node(path.empty() ? "." : path) : nullptr;
    if (!b.node) {
        Unresolved(track.path + " (no such node)");
        return b;
    }
    const std::string& p = track.property;
    bool ok = false;
    switch (track.type) {
    case TrackType::Method:
        b.prop = Property::Method;
        ok = true;
        break;
    case TrackType::Audio:
        b.prop = Property::Audio;
        ok = b.node->as_audio_playback() != nullptr;
        break;
    case TrackType::Position3D:
    case TrackType::Rotation3D:
    case TrackType::Scale3D:
        // A subname on a Skeleton3D is a bone (animation_mixer: skeleton bone tracks).
        if (auto* sk = cast_to<Skeleton3D>(b.node); sk && !p.empty()) {
            b.bone = sk->find_bone(p);
            b.prop = track.type == TrackType::Position3D   ? Property::BonePosition
                     : track.type == TrackType::Rotation3D ? Property::BoneRotation
                                                           : Property::BoneScale;
            ok = b.bone >= 0;
            break;
        }
        b.prop = track.type == TrackType::Position3D   ? Property::Position
                 : track.type == TrackType::Rotation3D ? Property::Rotation
                                                       : Property::Scale;
        ok = cast_to<Node3D>(b.node);
        break;
    case TrackType::Value:
        if ((p == "visible" || p == "position") && cast_to<Control>(b.node)) {
            b.prop = p == "visible" ? Property::ControlVisible : Property::ControlPosition;
            ok = true;
        } else if ((p == "modulate" || p == "self_modulate" || p == "rotation") && cast_to<Control>(b.node)) {
            b.prop = p == "modulate" ? Property::CanvasModulate
                   : p == "self_modulate" ? Property::CanvasSelfModulate : Property::ControlRotation;
            ok = true;
        } else if (p == "color" && cast_to<ColorRect>(b.node)) {
            b.prop = Property::RectColor;
            ok = true;
        } else if (p == "theme_override_colors/font_color" && cast_to<Label>(b.node)) {
            b.prop = Property::FontColor;
            ok = true;
        } else if (p == "position" || p == "rotation" || p == "rotation_degrees" || p == "scale" ||
            p == "visible") {
            b.prop = p == "position" ? Property::Position
                   : p == "rotation" ? Property::Rotation
                   : p == "rotation_degrees" ? Property::RotationDegrees
                   : p == "scale" ? Property::Scale : Property::Visible;
            ok = cast_to<Node3D>(b.node);
        } else if (p == "transparency") {
            b.prop = Property::Transparency;
            ok = cast_to<GeometryInstance3D>(b.node);
        } else if (p == "layers") {
            b.prop = Property::Layers;
            ok = cast_to<VisualInstance3D>(b.node);
        } else if (p == "fov") {
            b.prop = Property::Fov;
            ok = cast_to<Camera3D>(b.node);
        } else if (p == "emitting") {
            b.prop = Property::Emitting;
            ok = cast_to<GPUParticles3D>(b.node);
        } else if (p == "light_energy" || p == "omni_range") {
            b.prop = p == "light_energy" ? Property::LightEnergy : Property::OmniRange;
            ok = cast_to<OmniLight3D>(b.node);
        } else if (p == "environment:adjustment_brightness" || p == "environment:adjustment_contrast" ||
                   p == "environment:adjustment_saturation") {
            b.prop = p == "environment:adjustment_brightness" ? Property::EnvBrightness
                   : p == "environment:adjustment_contrast"   ? Property::EnvContrast
                                                              : Property::EnvSaturation;
            ok = cast_to<WorldEnvironment>(b.node);
        } else if (p == "material_override:albedo_color") {
            b.prop = Property::OverrideAlbedo;
            ok = cast_to<GeometryInstance3D>(b.node);
        } else if (StartsWith(p, "mesh:surface_") && EndsWith(p, "/material:albedo_color")) {
            b.prop = Property::SurfaceAlbedo;
            b.surface = std::atoi(p.c_str() + 13);
            ok = cast_to<GeometryInstance3D>(b.node);
        }
        break;
    default:
        // PORT: BEZIER (3 tracks) exports no handles; counted, not played.
        break;
    }
    if (!ok) {
        Unresolved(track.path + " (unsupported)");
        b.node = nullptr;
    }
    return b;
}

// _ensure_node_caches: resolved the first time an animation plays.
const std::vector<AnimationPlayer::BoundTrack>& AnimationPlayer::Bind(int anim) {
    // _node_removed -> clear_caches: a freed node leaves every cache, so all
    // animations re-bind by path (a freed target resolves to nothing).
    if (const SceneTree* tree = get_tree(); tree && tree->free_generation() != bound_generation_) {
        bound_generation_ = tree->free_generation();
        for (auto& b : bound_) b.clear();
        std::fill(bound_ready_.begin(), bound_ready_.end(), uint8_t{0});
    }
    const auto i = static_cast<std::size_t>(anim);
    if (!bound_ready_[i]) {
        bound_ready_[i] = 1u;
        const auto& tracks = data_->animations[i].tracks;
        bound_[i].reserve(tracks.size());
        for (const Track& t : tracks) bound_[i].push_back(BindTrack(t));
    }
    return bound_[i];
}

// godot: animation_player.cpp:1668-1757
void AnimationPlayer::play(const std::string& name_in, float custom_scale, bool from_end) {
    const std::string name = name_in.empty() ? assigned_ : name_in;
    const int anim = FindAnimation(name);
    if (anim < 0) {
        if (get_tree()) ++get_tree()->animation_stats().missing;
        return;
    }
    // No blend times in this project: blend.clear(). A different animation
    // (get_current_animation() is empty when not playing) stops the speakers
    // the old one played into.
    if (!(playing_ && current_ == anim)) ClearAudioStreams();
    current_ = anim;
    const double length = data_->animations[static_cast<std::size_t>(anim)].length;
    if (assigned_ != name) {
        pos_ = from_end ? length : 0.0;
    } else if (from_end && pos_ == 0.0) {
        pos_ = length;
    } else if (!from_end && pos_ == length) {
        pos_ = 0.0;
    }
    custom_scale_ = custom_scale;
    assigned_ = name;
    seeked_ = false;
    started_ = true;
    set_process_internal(true);
    playing_ = true;
    has_played_ = true;
    if (get_tree()) ++get_tree()->animation_stats().plays;
}

// _stop_internal(reset = true, keep_state). godot: animation_player.cpp:2045-2064
void AnimationPlayer::stop(bool keep_state) {
    ClearAudioStreams();
    if (keep_state) {
        pos_ = 0.0;
    } else {
        is_stopping_ = true;
        seek(0.0, true);
        is_stopping_ = false;
    }
    current_ = -1;
    custom_scale_ = 1.0f;
    set_process_internal(false);
    queued_.clear();   // animation_player.cpp:2062
    playing_ = false;
}

// godot: animation_player.cpp:1819-1836
void AnimationPlayer::seek(double time, bool update) {
    pos_ = time;
    if (current_ < 0) {
        if (!assigned_.empty()) current_ = FindAnimation(assigned_);
        if (current_ < 0) return;
    }
    seeked_ = true;
    if (update) Process(0.0);
}

// NOTIFICATION_READY. godot: animation_player.cpp:235-240
void AnimationPlayer::_ready() {
    if (FindAnimation(autoplay_) >= 0) {
        play(autoplay_);
        Process(0.0);
    }
}

void AnimationPlayer::_internal_process(double delta) {
    Process(delta);
}

// _animation_process + _animation_process2. godot: animation_player.cpp:1076-1110, 1265-1316
void AnimationPlayer::Process(double delta) {
    if (current_ < 0 || !data_) {
        set_process_internal(false);
        return;
    }
    end_reached_ = false;
    const bool started = started_;
    started_ = false;
    const bool seeked = seeked_;
    if (delta != 0.0) seeked_ = false;
    pending_.clear();

    const int before = current_;
    ProcessData(delta, seeked, started);
    if (before != current_) return;   // changed during processing
    FlushPending();

    // godot: animation_player.cpp:1287-1310
    if (end_reached_) {
        ClearAudioStreams();
        if (!queued_.empty()) {
            const std::string next = queued_.front();
            play(next);
            queued_.pop_front();
        } else {
            playing_ = false;
            set_process_internal(false);
        }
        end_reached_ = false;
    }
}

// godot: animation_player.cpp:1643-1649
void AnimationPlayer::queue(const std::string& name) {
    if (!is_playing()) {
        play(name);
    } else {
        queued_.push_back(name);
    }
}

// godot: animation_player.cpp:1008-1074
void AnimationPlayer::ProcessData(double delta_in, bool seeked, bool started) {
    const assets::Animation& a = data_->animations[static_cast<std::size_t>(current_)];
    double delta = delta_in * speed_scale_ * custom_scale_;
    double next = pos_ + delta;
    const bool backwards = std::signbit(delta);
    const double len = a.length;
    LoopedFlag looped = LoopedFlag::None;

    if (a.loopMode == kLoopLinear) {
        if (next < 0.0 && pos_ >= 0.0) looped = LoopedFlag::Start;
        if (next > len && pos_ <= len) looped = LoopedFlag::End;
        next = fposmod(next, len);
    } else {
        // LOOP_NONE. PORT: ping-pong takes this branch; no animation uses it.
        if (next < 0.0) next = 0.0;
        else if (next > len) next = len;
        delta = next - pos_;
    }

    const double prev = pos_;
    pos_ = next;
    const int anim = current_;
    ApplyTracks(anim, prev, pos_, delta, seeked, started, looped);

    if (a.loopMode != kLoopLinear) {
        if (anim != current_) return;
        if (!backwards && prev <= len && next == len) end_reached_ = true;
        if (backwards && prev >= 0.0 && next == 0.0) end_reached_ = true;
    }
}

// The key indices a DISCRETE value or METHOD track acts on this frame.
// godot: animation_player.cpp:707-722, 779-794
void AnimationPlayer::CollectKeys(const assets::Animation& a, int track, double prev, double time,
                                  double delta, bool seeked, bool started, FindMode seekMode,
                                  LoopedFlag looped) {
    indices_.clear();
    if (seeked) {
        const int k = track_find_key(a, track, time, seekMode);
        if (k >= 0) indices_.push_back(k);
        return;
    }
    if (started) {
        const int k = track_find_key(a, track, prev, FindMode::Exact);
        if (k >= 0) indices_.push_back(k);
    }
    track_get_key_indices_in_range(a, track, time, delta, looped, indices_);
}

// godot: animation_player.cpp:491-1006
void AnimationPlayer::ApplyTracks(int anim, double prev, double time, double delta, bool seeked,
                                  bool started, LoopedFlag looped) {
    const assets::Animation& a = data_->animations[static_cast<std::size_t>(anim)];
    const std::vector<BoundTrack>& bound = Bind(anim);
    for (int i = 0; i < static_cast<int>(a.tracks.size()); ++i) {
        const Track& t = a.tracks[static_cast<std::size_t>(i)];
        const BoundTrack& b = bound[static_cast<std::size_t>(i)];
        if (!b.node || t.keys.empty()) continue;
        Value v;
        switch (t.type) {
        case TrackType::Position3D:
        case TrackType::Scale3D: {
            Vector3 p;
            const bool ok = t.type == TrackType::Position3D ? position_track_interpolate(a, i, time, p)
                                                           : scale_track_interpolate(a, i, time, p);
            if (!ok) break;
            v.kind = assets::ValueKind::Vec3;
            v.f[0] = p.x; v.f[1] = p.y; v.f[2] = p.z;
            pending_.push_back({b, v, true});
            break;
        }
        case TrackType::Rotation3D: {
            Quaternion q;
            if (!rotation_track_interpolate(a, i, time, q)) break;
            if (b.prop == Property::BoneRotation) {   // a bone keeps its quaternion
                v.f[0] = q.x; v.f[1] = q.y; v.f[2] = q.z; v.f[3] = q.w;
                pending_.push_back({b, v, true});
                break;
            }
            const Vector3 e = quat_to_euler_yxz(q);
            v.kind = assets::ValueKind::Vec3;
            v.f[0] = e.x; v.f[1] = e.y; v.f[2] = e.z;
            pending_.push_back({b, v, true});
            break;
        }
        case TrackType::Value:
            if (t.updateMode == assets::UpdateMode::Discrete) {
                CollectKeys(a, i, prev, time, delta, seeked, started, FindMode::Nearest, looped);
                for (int k : indices_) SetProperty(b, t.keys[static_cast<std::size_t>(k)].value);
            } else if (value_track_interpolate(a, i, time, v)) {
                // PORT: CAPTURE blends from a captured value before the first key;
                // animations.json has 0 capture tracks, so it samples continuously.
                pending_.push_back({b, v, false});
            }
            break;
        case TrackType::Method:
            if (is_stopping_ || !get_tree()) break;
            CollectKeys(a, i, prev, time, delta, seeked, started, FindMode::Exact, looped);
            for (int k : indices_) {
                const assets::Key& key = t.keys[static_cast<std::size_t>(k)];
                get_tree()->call_deferred(b.node, key.method, key.args);
            }
            break;
        case TrackType::Audio:
            if (!is_stopping_) PlayAudio(a, i, b, prev, time, delta, seeked, started, looped);
            break;
        default:
            break;
        }
    }
}

// godot: animation_player.cpp:830-919
void AnimationPlayer::PlayAudio(const assets::Animation& a, int track, const BoundTrack& b,
                                double prev, double time, double delta, bool seeked, bool started,
                                LoopedFlag looped) {
    (void)prev;
    int idx = -1;
    if (seeked || started) {
        idx = track_find_key(a, track, time, FindMode::Nearest);
    } else {
        indices_.clear();
        track_get_key_indices_in_range(a, track, time, delta, looped, indices_);
        if (!indices_.empty()) idx = indices_.back();
    }
    if (idx < 0) return;
    const assets::Key& key = a.tracks[static_cast<std::size_t>(track)].keys[static_cast<std::size_t>(idx)];
    if (key.stream.empty()) return;
    // animation_player.cpp:879-912: the key's stream joins the player's
    // polyphonic playback from its start_offset (plus how far into the key a
    // seek or start lands), stopped end_offset before the stream's end.
    float start = key.startOffset;
    if (seeked || started) start += static_cast<float>(time - key.time);
    AudioPlayback* speaker = b.node->as_audio_playback();
    speaker->play_from_animation(FileName(key.stream), start, key.endOffset);
    const uint64_t generation = get_tree() ? get_tree()->free_generation() : 0;
    if (generation != audio_generation_) {   // a node was freed since: the old list may dangle
        audio_players_.clear();
        audio_generation_ = generation;
    }
    if (std::find(audio_players_.begin(), audio_players_.end(), speaker) == audio_players_.end()) {
        audio_players_.push_back(speaker);
    }
}

// godot: animation_player.cpp:1922-1928
void AnimationPlayer::ClearAudioStreams() {
    const uint64_t generation = get_tree() ? get_tree()->free_generation() : 0;
    if (generation == audio_generation_) {
        for (AudioPlayback* speaker : audio_players_) {
            speaker->stop();
            speaker->set_stream("");
        }
    }
    audio_players_.clear();
}

// _animation_update_transforms: transforms first, then properties.
// godot: animation_player.cpp:1112-1208
void AnimationPlayer::FlushPending() {
    for (const PendingValue& p : pending_) {
        if (p.transform) SetProperty(p.track, p.value);
    }
    for (const PendingValue& p : pending_) {
        if (!p.transform) SetProperty(p.track, p.value);
    }
}

void AnimationPlayer::SetProperty(const BoundTrack& b, const Value& v) {
    const Vector3 v3{v.f[0], v.f[1], v.f[2]};
    const Color color{v.f[0], v.f[1], v.f[2], v.f[3]};
    switch (b.prop) {
    case Property::Position:        cast_to<Node3D>(b.node)->set_position(v3); break;
    case Property::BonePosition:    cast_to<Skeleton3D>(b.node)->set_bone_pose_position(b.bone, v3); break;
    case Property::BoneRotation:
        cast_to<Skeleton3D>(b.node)->set_bone_pose_rotation(b.bone, {v.f[0], v.f[1], v.f[2], v.f[3]});
        break;
    case Property::BoneScale:       cast_to<Skeleton3D>(b.node)->set_bone_pose_scale(b.bone, v3); break;
    case Property::Rotation:        cast_to<Node3D>(b.node)->set_rotation(v3); break;
    case Property::RotationDegrees: cast_to<Node3D>(b.node)->set_rotation_degrees(v3); break;
    case Property::Scale:           cast_to<Node3D>(b.node)->set_scale(v3); break;
    case Property::Visible:         cast_to<Node3D>(b.node)->set_visible(v.AsBool()); break;
    case Property::Transparency:
        cast_to<GeometryInstance3D>(b.node)->set_transparency(v.AsFloat());
        break;
    case Property::Layers:
        cast_to<VisualInstance3D>(b.node)->set_layers(static_cast<uint32_t>(v.AsInt()));
        break;
    case Property::Fov:         cast_to<Camera3D>(b.node)->set_fov(v.AsFloat()); break;
    case Property::LightEnergy: cast_to<OmniLight3D>(b.node)->set_light_energy(v.AsFloat()); break;
    case Property::OmniRange:   cast_to<OmniLight3D>(b.node)->set_omni_range(v.AsFloat()); break;
    case Property::EnvBrightness: cast_to<WorldEnvironment>(b.node)->set_adjustment_brightness(v.AsFloat()); break;
    case Property::EnvContrast:   cast_to<WorldEnvironment>(b.node)->set_adjustment_contrast(v.AsFloat()); break;
    case Property::EnvSaturation: cast_to<WorldEnvironment>(b.node)->set_adjustment_saturation(v.AsFloat()); break;
    case Property::Emitting:    cast_to<GPUParticles3D>(b.node)->set_emitting(v.AsBool()); break;
    case Property::SurfaceAlbedo:
        cast_to<GeometryInstance3D>(b.node)->set_surface_albedo(b.surface, color);
        break;
    case Property::OverrideAlbedo:
        cast_to<GeometryInstance3D>(b.node)->set_override_albedo(color);
        break;
    case Property::ControlVisible:  cast_to<Control>(b.node)->set_visible(v.AsBool()); break;
    case Property::ControlPosition: cast_to<Control>(b.node)->set_position({v.f[0], v.f[1]}); break;
    case Property::RectColor:       cast_to<ColorRect>(b.node)->set_color(color); break;
    case Property::FontColor:       cast_to<Label>(b.node)->set_font_color_override(color); break;
    case Property::CanvasModulate:     cast_to<Control>(b.node)->set_modulate(color); break;
    case Property::CanvasSelfModulate: cast_to<Control>(b.node)->set_self_modulate(color); break;
    case Property::ControlRotation:    cast_to<Control>(b.node)->set_rotation(v.AsFloat()); break;
    default:
        break;
    }
}

}  // namespace godot
