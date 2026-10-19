// A playing sound's per-channel gain as Godot 4.1.4 moves it: a new value is
// reached linearly across one 512-frame mix buffer, from wherever the gain is
// now (audio_server.cpp:638-648), so a fade or a 3D speaker heard from a
// moving camera never steps. The audio thread calls Next() once per output
// frame and SetTarget once per buffer, with the target the game thread
// published (platform::Audio's atomics).
#pragma once

#include "core/godot_mix.h"

#include <cstdint>

namespace core {

class GainRamp {
public:
    // Jumps straight to (l, r): a stream's first gain (its start ramp is
    // StartRamp's), or a reset.
    void Reset(float l, float r) {
        fromL_ = curL_ = toL_ = l;
        fromR_ = curR_ = toR_ = r;
        step_ = kGodotMixBuffer;
    }
    void SetTarget(float l, float r) {
        if (l == toL_ && r == toR_) return;
        fromL_ = curL_;
        fromR_ = curR_;
        toL_ = l;
        toR_ = r;
        step_ = 0;
    }
    void Next(float& l, float& r) {
        if (step_ >= kGodotMixBuffer) {
            curL_ = toL_;
            curR_ = toR_;
        } else {
            const float t = static_cast<float>(step_++) / static_cast<float>(kGodotMixBuffer);
            curL_ = fromL_ + (toL_ - fromL_) * t;
            curR_ = fromR_ + (toR_ - fromR_) * t;
        }
        l = curL_;
        r = curR_;
    }

private:
    float fromL_ = 1.0f, fromR_ = 1.0f;
    float curL_ = 1.0f, curR_ = 1.0f;
    float toL_ = 1.0f, toR_ = 1.0f;
    uint32_t step_ = kGodotMixBuffer;
};

}  // namespace core
