#include "seq/scheduler.h"

#include <algorithm>

namespace seq {

namespace {
thread_local Scheduler* g_current = nullptr;
}

Scheduler* Current() { return g_current; }

void SetCurrent(Scheduler* s) { g_current = s; }

void Scheduler::Spawn(Task&& t, const void* owner) {
    // Task suspends at initial_suspend, so the body has not run yet. Resume it
    // here, inside the scope where this scheduler is Current(), so the first
    // co_await registers against it. See the comment on initial_suspend().
    ScopedCurrent scope(this);

    if (!t.Valid()) return;

    t.Get().promise().generation = generation_;
    const void* outer = runningOwner_;   // a spawn from inside another sequence
    runningOwner_ = owner;
    t.Get().resume();                   // runs the body to its first co_await
    runningOwner_ = outer;

    if (t.Done()) return;               // no suspension points; nothing to keep
    owned_.push_back(Owned{std::move(t), owner});
}

void Scheduler::AddPending(std::coroutine_handle<> h, double seconds, bool scaled) {
    pending_.push_back(Pending{h, seconds, scaled, generation_, runningOwner_});
}

bool Scheduler::IsCancelled(const void* owner) const {
    return owner && std::find(cancelledOwners_.begin(), cancelledOwners_.end(), owner) != cancelledOwners_.end();
}

void Scheduler::CancelOwner(const void* owner) {
    if (!owner) return;
    // Raw handles, no ownership: dropping them is always safe.
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [owner](const Pending& p) { return p.owner == owner; }),
                   pending_.end());
    cancelledOwners_.push_back(owner);
    if (!ticking_) ReapCancelled();
}

// Releases the frames of every cancelled owner. Only outside a resume, so the
// frame that asked for the cancel is never freed under itself.
void Scheduler::ReapCancelled() {
    if (cancelledOwners_.empty()) return;
    // A cancelled sequence may have awaited AGAIN after its owner was
    // cancelled (it cancelled itself): that entry points into a frame about
    // to be released.
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [this](const Pending& p) { return IsCancelled(p.owner); }),
                   pending_.end());
    owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                                [this](const Owned& o) { return IsCancelled(o.owner); }),
                 owned_.end());
    cancelledOwners_.clear();
}

void Scheduler::Drain() {
    // pending_ holds raw handles to the same frames owned_ holds. Clearing
    // pending_ must not destroy them; owned_'s Task destructors do that
    // exactly once, and destroying here as well would be a double free.
    pending_.clear();
    owned_.clear();
}

void Scheduler::CancelAll() {
    // Bumping the generation is what actually stops execution: any pending
    // entry stamped with an older generation is skipped instead of resumed.
    ++generation_;

    if (ticking_) {
        // This may be inside the very sequence that asked to cancel (the shot
        // that kills the player). Destroying frames now would free the one
        // executing, so it waits until the Tick unwinds.
        cancelRequested_ = true;
        pending_.clear();          // safe: raw handles, no ownership
        return;
    }
    Drain();
}

void Scheduler::Tick(double dt) {
    ScopedCurrent scope(this);
    ticking_ = true;

    // Collect first, resume second. A resumed sequence can append to pending_
    // (its next co_await) or spawn new tasks, and iterating a vector while it
    // grows is undefined.
    std::vector<Pending> ready;
    for (std::size_t i = 0; i < pending_.size();) {
        Pending& p = pending_[i];

        const double step = p.scaled ? (paused_ ? 0.0 : dt * timeScale_) : dt;
        p.remaining -= step;

        if (p.remaining <= 0.0) {
            ready.push_back(p);
            pending_[i] = pending_.back();   // order within a frame is by
            pending_.pop_back();             // completion, not by insertion
        } else {
            ++i;
        }
    }

    // Several delays can expire in the same tick. Resume the most overdue
    // first, the order they would have fired in with an infinitely fine
    // timestep; it also makes the result deterministic, where the swap-and-pop
    // above would leave same-tick ordering arbitrary.
    std::stable_sort(ready.begin(), ready.end(),
                     [](const Pending& a, const Pending& b) {
                         return a.remaining < b.remaining;
                     });

    for (const Pending& r : ready) {
        if (r.generation != generation_) continue;   // aborted mid-flight
        if (IsCancelled(r.owner)) continue;          // its owner was freed this tick
        runningOwner_ = r.owner;
        if (r.h && !r.h.done()) r.h.resume();
        runningOwner_ = nullptr;
    }

    ticking_ = false;
    ReapCancelled();

    // A sequence cancelled the round from inside its own body. Now that the
    // resume loop has unwound, no frame is executing and it is safe to free.
    if (cancelRequested_) {
        cancelRequested_ = false;
        Drain();
        return;
    }

    // Reap finished frames. Task's destructor releases the coroutine frame.
    for (std::size_t i = 0; i < owned_.size();) {
        if (owned_[i].task.Done()) {
            owned_[i] = std::move(owned_.back());
            owned_.pop_back();
        } else {
            ++i;
        }
    }
}

}  // namespace seq
