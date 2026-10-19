// Frame timing.
//
// The clamp matters: seq::Scheduler advances every pending delay by dt, so a
// single huge dt (a debugger breakpoint, a window drag, a disk stall) would
// expire a whole dealer turn's worth of delays in one tick and the round would
// visibly skip. Clamping trades a moment of slow motion for never losing the
// animation, the right trade for a turn-based game.
#pragma once

#include "platform/gl_compat.h"

namespace platform {

class Clock {
public:
    // 0.25s = 15 frames at 60fps. Long enough that ordinary hitching passes
    // through untouched, short enough that a stall cannot skip a turn.
    static constexpr float kMaxDelta = 0.25f;

    Clock() : last_(SDL_GetPerformanceCounter()),
              freq_(static_cast<double>(SDL_GetPerformanceFrequency())) {}

    // Seconds since the previous call, clamped.
    float Tick() {
        const Uint64 now = SDL_GetPerformanceCounter();
        double dt = static_cast<double>(now - last_) / freq_;
        last_ = now;
        if (dt < 0.0) dt = 0.0;                 // counter wrap; treat as no time
        if (dt > kMaxDelta) dt = kMaxDelta;
        return static_cast<float>(dt);
    }

private:
    Uint64 last_;
    double freq_;
};

}  // namespace platform
