// Godot 4.1's Animation sampling and key selection, transcribed from
// scene/resources/animation.cpp at 4.1.4-stable. assets::Animation holds the
// data; these are the rules Godot applies to it. parity_anim_sample diffs every
// track of every animation in main.tscn against the engine.
//
// Forward playback only, and loop modes NONE and LINEAR only: animations.json
// has 181 non-looping and 15 linear-looping animations, no ping-pong, and no
// script plays an animation backwards. Where Godot handles those cases, the
// transcription says PORT and takes the forward/linear branch.
#pragma once

#include "assets/animation.h"
#include "godot/math.h"

#include <vector>

namespace godot {

// Animation::FindMode.
enum class FindMode { Nearest, Approx, Exact };

// Animation::LoopedFlag, set by AnimationPlayer when the playhead wrapped.
enum class LoopedFlag { None, End, Start };

// Animation::value_track_interpolate. False when the track has no usable value.
bool value_track_interpolate(const assets::Animation& anim, int track, double time,
                             assets::Value& out);

// Animation::try_position/rotation/scale_track_interpolate.
bool position_track_interpolate(const assets::Animation& anim, int track, double time,
                                Vector3& out);
bool rotation_track_interpolate(const assets::Animation& anim, int track, double time,
                                Quaternion& out);
bool scale_track_interpolate(const assets::Animation& anim, int track, double time,
                             Vector3& out);

// Animation::track_find_key. -1 when no key qualifies.
int track_find_key(const assets::Animation& anim, int track, double time, FindMode mode);

// Animation::track_get_key_indices_in_range: the keys the playhead crossed
// moving `delta` to reach `time`. Appends to `out`.
void track_get_key_indices_in_range(const assets::Animation& anim, int track, double time,
                                    double delta, LoopedFlag looped, std::vector<int>& out);

}  // namespace godot
