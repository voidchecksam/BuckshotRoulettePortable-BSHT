// Drives suspended game sequences forward.
//
// One instance owns every pending sequence; Tick() is called once per frame.
// This is the machinery the 351 GDScript `await get_tree().create_timer(...)`
// sites map onto.
#pragma once

#include "seq/task.h"

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace seq {

class Scheduler {
public:
    Scheduler() = default;

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Takes ownership. Detached / fire-and-forget: the caller does not await
    // the result. ShellSpawner.gd launches PlayAudioIndicators() exactly this
    // way, so overlapping sequences are a requirement, not a nicety.
    //
    // `owner` is the object whose method started the sequence (a GDScript
    // function state belongs to its instance). It follows the sequence through
    // every co_await, awaited children included.
    void Spawn(Task&& t, const void* owner = nullptr);

    // The owner was freed: its sequences never resume. Safe inside a Tick --
    // frames are released once the Tick unwinds. A null owner cancels nothing.
    void CancelOwner(const void* owner);

    // Advance time. dt is RAW seconds; scaling and pause are applied internally
    // so callers always pass real frame delta.
    void Tick(double dt);

    // Sequences currently suspended on a Delay.
    std::size_t PendingCount() const { return pending_.size(); }

    // Frames still owned by the scheduler, running or suspended.
    std::size_t LiveTaskCount() const { return owned_.size(); }

    std::uint32_t Generation() const { return generation_; }

    // Abort every in-flight sequence. Safe to call from inside a running
    // sequence: frame destruction waits for the end of the current Tick, so
    // the frame that is executing is never freed.
    void CancelAll();

    // Slow motion and pause. Delay honours both; DelayUnscaled ignores both so
    // UI and pause-menu sequences keep running while the game is frozen.
    void  SetTimeScale(float s) { timeScale_ = (s < 0.0f ? 0.0f : s); }
    float TimeScale() const     { return timeScale_; }
    void  SetPaused(bool p)     { paused_ = p; }
    bool  Paused() const        { return paused_; }

    // Registration hook used by the Delay awaitables.
    void AddPending(std::coroutine_handle<> h, double seconds, bool scaled);

private:
    struct Pending {
        std::coroutine_handle<> h;
        double                  remaining;
        bool                    scaled;
        std::uint32_t           generation;
        const void*             owner;
    };
    struct Owned {
        Task        task;
        const void* owner;
    };
    bool IsCancelled(const void* owner) const;
    void ReapCancelled();

    // Releases every frame. pending_ holds raw handles to frames that owned_
    // also holds, so pending_ is cleared without destroying; Task's destructor
    // does it exactly once.
    void Drain();

    std::vector<Pending> pending_;
    std::vector<Owned>   owned_;          // keeps detached frames alive
    std::vector<const void*> cancelledOwners_;   // cancelled during this Tick
    const void*          runningOwner_ = nullptr;   // owner of the frame now executing
    std::uint32_t        generation_ = 0;
    float                timeScale_  = 1.0f;
    bool                 paused_     = false;
    bool                 ticking_    = false;
    bool                 cancelRequested_ = false;
};

// Awaitables need the scheduler that is currently running them. Thread-local
// so a future asset-loading thread cannot corrupt the game scheduler.
Scheduler* Current();
void       SetCurrent(Scheduler* s);

// Sets Current() for a scope and restores the previous value on exit, so
// nesting Spawn() inside a running sequence behaves correctly.
class ScopedCurrent {
public:
    explicit ScopedCurrent(Scheduler* s) : prev_(Current()) { SetCurrent(s); }
    ~ScopedCurrent() { SetCurrent(prev_); }

    ScopedCurrent(const ScopedCurrent&)            = delete;
    ScopedCurrent& operator=(const ScopedCurrent&) = delete;

private:
    Scheduler* prev_;
};

}  // namespace seq
