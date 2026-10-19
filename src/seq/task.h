// Coroutine return type for game sequences.
//
// A Task owns its coroutine frame and destroys it on destruction. The body runs
// eagerly up to the first co_await, which matches GDScript: statements before
// `await get_tree().create_timer(...)` execute immediately.
//
// final_suspend() keeps the frame alive after completion long enough for the
// scheduler to observe done() and reap it. Self-destroying there would leave the
// scheduler holding a dangling handle.
//
// A Task can also be awaited from inside another sequence:
//
//     co_await Child(args);        <=>  await Child(args)
//
// The child starts at once in the same frame, and the awaiting sequence resumes
// the instant the child finishes, not a frame later, because the child's final
// suspend transfers straight back to it. The original leans on this
// everywhere: ShotgunShooting.Shoot awaits DeathManager.Kill, which awaits
// HealthCounter.UpdateDisplayRoutine.
#pragma once

#include <coroutine>
#include <cstdint>
#include <utility>

namespace seq {

struct Task {
    struct promise_type {
        // Stamped when the task is spawned; compared on resume so a sequence
        // belonging to an aborted round is dropped instead of continued.
        std::uint32_t generation = 0;

        // The sequence awaiting this one, if any. Null for a detached task.
        std::coroutine_handle<> continuation;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        // suspend_always, not suspend_never.
        //
        // The Task is constructed during argument evaluation at the call site:
        // `s.Spawn(MySequence())` builds it before Spawn is entered. With
        // suspend_never the body would run right there, before the scheduler
        // had installed itself as Current(), so the first co_await would find
        // no scheduler. Suspending here lets Spawn() do the first resume inside
        // its own scope. To the caller nothing changes: once Spawn() returns,
        // the body has run up to its first co_await, as in GDScript.
        std::suspend_always initial_suspend() noexcept { return {}; }

        // Suspends, then hands control to the awaiting sequence if there is
        // one. Symmetric transfer, so a chain of awaited sequences finishing
        // together does not grow the stack.
        struct FinalAwaiter {
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> self) const noexcept {
                const std::coroutine_handle<> next = self.promise().continuation;
                return next ? next : std::noop_coroutine();
            }
            void await_resume() const noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_void() {}
        // Game logic is written not to throw; swallowing here keeps a rogue
        // exception from calling std::terminate on a handheld.
        void unhandled_exception() {}
    };

    using Handle = std::coroutine_handle<promise_type>;

    Task() = default;
    explicit Task(Handle h) : h_(h) {}

    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    Task& operator=(Task&& o) noexcept {
        if (this != &o) {
            Destroy();
            h_ = std::exchange(o.h_, {});
        }
        return *this;
    }

    ~Task() { Destroy(); }

    bool   Valid() const { return static_cast<bool>(h_); }
    bool   Done()  const { return !h_ || h_.done(); }
    Handle Get()   const { return h_; }

    // Relinquish ownership without destroying.
    Handle Release() { return std::exchange(h_, {}); }

    // `co_await Child(...)`. Only on an rvalue: the child frame belongs to the
    // temporary, which lives in the awaiting frame until the await completes,
    // so cancelling the parent destroys the child with it.
    auto operator co_await() && noexcept {
        struct Awaiter {
            Handle child;
            bool await_ready() const noexcept { return !child || child.done(); }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) const noexcept {
                child.promise().continuation = parent;
                return child;   // start the child now, in this frame
            }
            void await_resume() const noexcept {}
        };
        return Awaiter{h_};
    }

private:
    void Destroy() {
        if (h_) {
            h_.destroy();
            h_ = {};
        }
    }

    Handle h_{};
};

}  // namespace seq
