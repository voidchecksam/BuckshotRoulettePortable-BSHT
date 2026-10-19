// Animation data: tracks and keyframes as Godot exported them.
//
// Most tracks are VALUE tracks, and a handful of properties (position,
// rotation, visible) make up most of those. So this is a property animation
// system, not a skeletal one, and the data is small enough that nothing here
// needs to be clever.
//
// Sampling and playback are Godot's and live in godot/animation.* and
// godot/animation_player.*. This file is data only.
//
// Headless: no GL, no SDL, so parsing is unit-testable and identical on
// arm-vita-eabi.
#pragma once

#include "core/mat4.h"

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

// animations.json must carry this. Version 1 had no update modes, loop wrap or
// key transitions, and would load as plausible, wrong motion.
inline constexpr int kAnimationFormat = 2;

// Mirrors Godot's Animation.TrackType for the types this game actually uses.
enum class TrackType : uint8_t {
    Value       = 0,
    Position3D  = 1,
    Rotation3D  = 2,
    Scale3D     = 3,
    Method      = 5,
    Bezier      = 6,
    Audio       = 7,
    Unsupported = 255,
};

// Godot's interpolation modes. Only Nearest, Linear and Cubic occur (5, 758 and
// 263 tracks respectively).
enum class Interp : uint8_t {
    Nearest = 0,
    Linear  = 1,
    Cubic   = 2,
};

// Godot's Animation::UpdateMode. CONTINUOUS is the default; DISCRETE samples
// nearest and sets only the keys the playhead crosses; CAPTURE blends from the
// property's value captured when the animation started.
enum class UpdateMode : uint8_t {
    Continuous = 0,
    Discrete   = 1,
    Capture    = 2,
};

// The value kinds the exporter tags. Keeping the kind explicit means a consumer
// never has to infer the type from the property name, which would break the
// moment a property is animated with an unexpected type.
enum class ValueKind : uint8_t {
    None, Bool, Int, Float, Vec2, Vec3, Quat, Color, Str, Unknown,
};

// A single keyframe value. Small enough to store inline; there are only 4,328
// of them in the entire game.
struct Value {
    ValueKind kind = ValueKind::None;
    float     f[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::string s;

    bool  AsBool()  const { return f[0] != 0.0f; }
    int   AsInt()   const { return static_cast<int>(f[0]); }
    float AsFloat() const { return f[0]; }
};

struct Key {
    float time = 0.0f;
    Value value;

    // Method tracks only.
    std::string        method;
    std::vector<Value> args;

    // Audio tracks only.
    std::string stream;

    // Godot's key transition: the blend factor is eased by it, and 0 holds this
    // key's value until the next one. LAST, so existing positional initialisers
    // of {time, value, method, args, stream} keep their meaning.
    float transition = 1.0f;

    // Audio tracks only: Animation::audio_track_get_key_start_offset /
    // end_offset, seconds trimmed off the stream's head and tail. Last, for
    // the same reason.
    float startOffset = 0.0f;
    float endOffset = 0.0f;
};

struct Track {
    TrackType   type   = TrackType::Unsupported;
    Interp      interp = Interp::Linear;
    UpdateMode  updateMode = UpdateMode::Continuous;   // VALUE tracks only
    bool        loopWrap = true;

    // Full Godot path, e.g. "shotgun parent/shotgun:position". `nodePath` and
    // `property` are the split halves, precomputed because the runtime resolves
    // by node and then sets by property.
    std::string path;
    std::string nodePath;
    std::string property;

    std::vector<Key> keys;

    bool Empty() const { return keys.empty(); }
    float Length() const { return keys.empty() ? 0.0f : keys.back().time; }
};

struct Animation {
    std::string name;
    float length = 0.0f;
    float step   = 0.0f;
    int   loopMode = 0;          // Godot: 0 none, 1 linear, 2 pingpong
    std::vector<Track> tracks;

    bool Loops() const { return loopMode != 0; }
};

struct AnimationPlayerData {
    std::string path;            // the player's own node path in the scene

    // The node that track paths resolve against. It defaults to the player's
    // parent in Godot but need not be, and one player in this scene sets it
    // higher. A track whose node is missing does nothing, silently.
    std::string root;

    std::string autoplay;
    std::vector<Animation> animations;
};

struct AnimationStats {
    int players    = 0;
    int resolvable = 0;          // tracks whose node path is non-empty
    int animations = 0;
    int tracks     = 0;
    int keys       = 0;
    int methodKeys = 0;
    int audioKeys  = 0;
    int unsupportedTracks = 0;   // counted, never silently dropped
};

class AnimationLibrary {
public:
    bool LoadFromFile(const char* path);
    // False on a missing or old `format` stamp.
    bool LoadFromString(const char* json);

    // nullptr when absent, so a typo is caught rather than silently doing
    // nothing at the moment the game expected a shell to eject.
    const Animation* Find(const char* playerPath, const char* animName) const;
    const AnimationPlayerData* FindPlayer(const char* playerPath) const;

    const std::vector<AnimationPlayerData>& Players() const { return players_; }
    const AnimationStats& Stats() const { return stats_; }

private:
    std::vector<AnimationPlayerData> players_;
    AnimationStats stats_;
};

}  // namespace assets
