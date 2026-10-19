// Godot 4.1's AnimationPlayer, transcribed from scene/animation/animation_player.cpp
// at 4.1.4-stable. parity_anim_play_semantics pins its playback rules against
// the engine; parity_anim_sample pins the sampling underneath.
//
// Rules worth knowing before changing anything here:
//   - play() processes nothing. The player starts processing, so it advances on
//     the next frame, or this frame if it was already processing and sits
//     later in the tree than the caller.
//   - play() of the animation already assigned continues from its position,
//     unless that position is the end.
//   - METHOD keys are called deferred (the default method_call_mode, which no
//     player in main.tscn overrides): after every node has processed.
//   - Continuous values are applied after the track loop, transforms first;
//     discrete values and audio happen during it.
//   - stop() seeks to 0 and applies it at once, without calling methods.
//   - A non-looping animation ends the frame its position first clamps to the
//     length, so the end key fires once and playing drops a frame later when
//     the position accumulated to the length exactly.
//
// No blending: this project sets no blend times. No backward play. queue() is
// 4.1.4's (HeavenManager uses it): play at once when idle, else play when the
// current animation ends; stop() and pause() empty it (_stop_internal).
#pragma once

#include "assets/animation.h"
#include "godot/animation.h"
#include "godot/node.h"

#include <deque>
#include <string>
#include <vector>

namespace godot {

class AnimationPlayer : public Node {
public:
    static constexpr uint32_t kClassBit = kClassAnimationPlayer;
    explicit AnimationPlayer(std::string name)
        : Node(std::move(name), kClassAnimationPlayer) { set_class("AnimationPlayer"); }

    // The player's animations; must outlive the player.
    void set_animations(const assets::AnimationPlayerData* data);
    const assets::AnimationPlayerData* get_animations() const { return data_; }
    bool has_animation(const std::string& name) const { return FindAnimation(name) >= 0; }
    // AnimationPlayer.get_animation: the named animation, or null (Godot
    // returns an empty Ref and errors). Its `length` is Animation.get_length().
    const assets::Animation* get_animation(const std::string& name) const {
        const int i = FindAnimation(name);
        return i >= 0 ? &data_->animations[static_cast<std::size_t>(i)] : nullptr;
    }

    // The exported state at the capture frame: a playing player resumes.
    void set_initial_state(std::string assigned, bool playing, double position,
                           float speed_scale, std::string autoplay);

    void play(const std::string& name = std::string(), float custom_scale = 1.0f,
              bool from_end = false);
    void stop(bool keep_state = false);
    void seek(double time, bool update = false);
    // godot: animation_player.cpp:1643-1664
    void queue(const std::string& name);
    void clear_queue() { queued_.clear(); }
    const std::deque<std::string>& get_queue() const { return queued_; }

    bool is_playing() const { return playing_; }
    // AnimationPlayer.pause (animation_player.cpp): playback stops where it
    // is; play() of the same animation resumes from there.
    void pause() {
        playing_ = false;
        queued_.clear();   // _stop_internal (animation_player.cpp:2062)
    }
    const std::string& get_assigned_animation() const { return assigned_; }
    std::string get_current_animation() const { return playing_ ? assigned_ : std::string(); }
    double get_current_animation_position() const { return current_ >= 0 ? pos_ : 0.0; }
    float get_speed_scale() const { return speed_scale_; }
    void  set_speed_scale(float s) { speed_scale_ = s; }
    const std::string& get_autoplay() const { return autoplay_; }
    bool has_played() const { return has_played_; }

    // Tracks whose node does not exist, or whose property the runtime does not
    // model, named once each. Counted when an animation first plays.
    int unresolved_tracks() const { return static_cast<int>(unresolved_paths_.size()); }
    const std::vector<std::string>& unresolved_paths() const { return unresolved_paths_; }

    void _ready() override;
    void _internal_process(double delta) override;

private:
    enum class Property : uint8_t {
        None, Position, Rotation, RotationDegrees, Scale, Visible, Transparency, Layers,
        Fov, LightEnergy, OmniRange, Emitting, SurfaceAlbedo, OverrideAlbedo, Method, Audio,
        EnvBrightness, EnvContrast, EnvSaturation,
        BonePosition, BoneRotation, BoneScale,
        // Controls (menu.tscn): CanvasItem.visible, Control.position,
        // ColorRect.color, Label's theme font_color override.
        ControlVisible, ControlPosition, RectColor, FontColor,
        // CanvasItem.modulate / self_modulate, Control.rotation.
        CanvasModulate, CanvasSelfModulate, ControlRotation,
    };
    struct BoundTrack {
        Node* node = nullptr;
        Property prop = Property::None;
        int surface = 0;
        int bone = -1;   // a Skeleton3D bone track ("Skeleton3D:bone")
    };
    struct PendingValue {
        BoundTrack track;
        assets::Value value;
        bool transform = false;
    };

    int FindAnimation(const std::string& name) const;
    std::deque<std::string> queued_;
    const std::vector<BoundTrack>& Bind(int anim);
    BoundTrack BindTrack(const assets::Track& track);
    void Unresolved(const std::string& what);

    void Process(double delta);
    void ProcessData(double delta, bool seeked, bool started);
    void ApplyTracks(int anim, double prev, double time, double delta, bool seeked, bool started,
                     LoopedFlag looped);
    void CollectKeys(const assets::Animation& a, int track, double prev, double time, double delta,
                     bool seeked, bool started, FindMode seekMode, LoopedFlag looped);
    void PlayAudio(const assets::Animation& a, int track, const BoundTrack& b, double prev,
                   double time, double delta, bool seeked, bool started, LoopedFlag looped);
    void FlushPending();
    static void SetProperty(const BoundTrack& b, const assets::Value& v);

    const assets::AnimationPlayerData* data_ = nullptr;
    std::vector<std::vector<BoundTrack>> bound_;
    std::vector<uint8_t> bound_ready_;
    uint64_t bound_generation_ = 0;   // SceneTree::free_generation the caches match
    std::string assigned_;
    std::string autoplay_;
    int current_ = -1;             // index into data_->animations; Godot's playback.current.from
    double pos_ = 0.0;
    float speed_scale_ = 1.0f;     // AnimationPlayer.speed_scale
    float custom_scale_ = 1.0f;    // playback.current.speed_scale
    bool playing_ = false;
    bool started_ = false;
    bool seeked_ = false;
    bool is_stopping_ = false;
    bool end_reached_ = false;
    // _clear_audio_streams (animation_player.cpp:1922-1928): the speakers this
    // player's AUDIO keys played into, stopped (their sounds fade) and their
    // stream cleared when its animation ends, another animation plays, or it
    // stops. Recorded with the tree's free generation: after a node is freed
    // the list is dropped untouched (Godot clears it in _node_removed).
    std::vector<AudioPlayback*> audio_players_;
    uint64_t audio_generation_ = 0;
    void ClearAudioStreams();
    bool has_played_ = false;
    std::vector<PendingValue> pending_;   // reused every frame
    std::vector<int> indices_;            // reused every frame
    std::vector<std::string> unresolved_paths_;
};

}  // namespace godot
