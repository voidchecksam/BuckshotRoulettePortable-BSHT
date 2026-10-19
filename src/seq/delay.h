// Suspension primitives for game sequences.
//
//   co_await Delay(0.3f);          <=>  await get_tree().create_timer(.3, false).timeout
//
// await_ready() returns false even for a zero-second wait: a zero delay still
// yields to the next frame, which is what Godot does and what several
// sequences depend on for ordering.
#pragma once

#include "seq/scheduler.h"

#include <coroutine>

namespace seq {

struct DelayBase {
    double seconds;   // double: SceneTreeTimer.time_left (scene_tree.cpp:570 at 4.1.4)
    bool  scaled;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) const {
        Scheduler* s = Current();
        if (s) {
            s->AddPending(h, seconds, scaled);
        } else {
            // No scheduler in scope: resuming immediately is wrong, but leaking
            // the frame is worse. This only happens if a sequence is awaited
            // outside Spawn()/Tick(), which is a programming error.
            h.resume();
        }
    }

    void await_resume() const noexcept {}
};

// Honours time scale and pause. The default, matching create_timer(t, false).
struct Delay : DelayBase {
    explicit Delay(double s) : DelayBase{s, true} {}
};

// Real time regardless of scaling or pause, for UI and pause-menu sequences
// that must keep running while the game itself is frozen.
struct DelayUnscaled : DelayBase {
    explicit DelayUnscaled(double s) : DelayBase{s, false} {}
};

}  // namespace seq
