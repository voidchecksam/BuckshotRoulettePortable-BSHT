// Scheduler semantics.
//
// These encode what the GDScript relies on, not what a coroutine library
// "should" do:
//   * a sequence body runs eagerly up to its first co_await, because statements
//     before `await get_tree().create_timer(...)` execute immediately in Godot
//   * a zero-second delay still yields to the next frame
//   * tasks can be launched detached: ShellSpawner.gd calls
//     PlayAudioIndicators() without await, so it overlaps its caller
//
// ---------------------------------------------------------------------------
// Never write a coroutine as a temporary lambda.
//
//     s.Spawn([&, i]() -> Task { co_await Delay(x); use(i); }());   // BROKEN
//
// Captured variables live in the closure object, not in the coroutine frame.
// The lambda temporary dies at the end of the full expression, so every
// capture dangles the moment the coroutine resumes. Reference captures often
// survive by luck; by-value ones corrupt.
//
// Coroutine parameters, by contrast, are copied into the frame and are safe.
// So sequences are written as free functions taking what they need:
//
//     static Task MySequence(std::vector<int>& out, int i) { ... }
//     s.Spawn(MySequence(out, i));                                  // CORRECT
//
// Every translated sequence follows the same rule.
// ---------------------------------------------------------------------------
#include <gtest/gtest.h>

#include "seq/delay.h"
#include "seq/scheduler.h"

#include <string>
#include <vector>

using namespace seq;

namespace {

using Log = std::vector<std::string>;

Task TwoStepLog(Log& log) {
    log.push_back("a");
    co_await Delay(0.5f);
    log.push_back("b");
}

Task LogAfter(Log& log, const char* what, float seconds) {
    co_await Delay(seconds);
    log.push_back(what);
}

Task ThreeStepOrder(std::vector<int>& log) {
    log.push_back(1);
    co_await Delay(0.3f);
    log.push_back(2);
    co_await Delay(0.7f);
    log.push_back(3);
}

Task SetAfter(int& out, int value, float seconds) {
    co_await Delay(seconds);
    out = value;
}

Task SetNow(int& out, int value) {
    out = value;
    co_return;
}

Task JustWait(float seconds) { co_await Delay(seconds); }

Task SetNowLogged(Log& log) {
    log.push_back("now");
    co_return;
}

Task RecordIndexAfter(std::vector<int>& out, int index) {
    co_await Delay(0.01f * static_cast<float>(index + 1));
    out.push_back(index);
}

Task InnerLogged(Log& log) {
    log.push_back("inner-start");
    co_await Delay(0.1f);
    log.push_back("inner-end");
}

Task OuterSpawningInner(Scheduler& s, Log& log) {
    log.push_back("outer-start");
    s.Spawn(InnerLogged(log));
    co_await Delay(0.3f);
    log.push_back("outer-end");
}

}  // namespace

TEST(Seq, RunsSynchronouslyUntilFirstAwait) {
    Scheduler s;
    Log log;
    s.Spawn(TwoStepLog(log));
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "a");
}

TEST(Seq, ResumesOnlyAfterEnoughTimeElapses) {
    Scheduler s;
    Log log;
    s.Spawn(LogAfter(log, "fired", 0.5f));
    s.Tick(0.2f);
    EXPECT_TRUE(log.empty());
    s.Tick(0.2f);
    EXPECT_TRUE(log.empty());
    s.Tick(0.2f);                       // total 0.6 > 0.5
    ASSERT_EQ(log.size(), 1u);
}

TEST(Seq, SequentialAwaitsFireInOrder) {
    Scheduler s;
    std::vector<int> log;
    s.Spawn(ThreeStepOrder(log));
    for (int i = 0; i < 100; ++i) s.Tick(0.016f);
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], 1);
    EXPECT_EQ(log[1], 2);
    EXPECT_EQ(log[2], 3);
}

TEST(Seq, DetachedTasksRunConcurrently) {
    Scheduler s;
    Log log;
    s.Spawn(LogAfter(log, "background", 0.1f));
    s.Spawn(LogAfter(log, "main", 0.2f));
    for (int i = 0; i < 30; ++i) s.Tick(0.016f);
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "background");    // shorter delay resumes first
    EXPECT_EQ(log[1], "main");
}

TEST(Seq, CompletedTasksLeaveThePendingList) {
    Scheduler s;
    s.Spawn(JustWait(0.1f));
    EXPECT_EQ(s.PendingCount(), 1u);
    for (int i = 0; i < 20; ++i) s.Tick(0.016f);
    EXPECT_EQ(s.PendingCount(), 0u);
    EXPECT_EQ(s.LiveTaskCount(), 0u);
}

TEST(Seq, ZeroDelayResumesOnNextTick) {
    Scheduler s;
    int n = 0;
    s.Spawn(SetAfter(n, 1, 0.0f));
    EXPECT_EQ(n, 0);
    s.Tick(0.016f);
    EXPECT_EQ(n, 1);
}

TEST(Seq, ASequenceWithNoAwaitCompletesImmediately) {
    Scheduler s;
    int n = 0;
    s.Spawn(SetNow(n, 1));
    EXPECT_EQ(n, 1);
    EXPECT_EQ(s.PendingCount(), 0u);
    EXPECT_EQ(s.LiveTaskCount(), 0u);
}

TEST(Seq, ManySequencesInterleaveCorrectly) {
    // Turn-based play never has many live at once, but the scheduler must not
    // care how many there are, and same-tick expiries must be deterministic.
    Scheduler s;
    std::vector<int> done;
    for (int i = 0; i < 32; ++i) s.Spawn(RecordIndexAfter(done, i));
    for (int i = 0; i < 60; ++i) s.Tick(0.016f);
    ASSERT_EQ(done.size(), 32u);
    for (int i = 0; i < 32; ++i) EXPECT_EQ(done[static_cast<size_t>(i)], i);
}

TEST(Seq, NestedSpawnFromInsideARunningSequence) {
    // Dealer turns launch helper sequences that themselves await.
    Scheduler s;
    Log log;
    s.Spawn(OuterSpawningInner(s, log));
    for (int i = 0; i < 40; ++i) s.Tick(0.016f);
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[0], "outer-start");
    EXPECT_EQ(log[1], "inner-start");
    EXPECT_EQ(log[2], "inner-end");
    EXPECT_EQ(log[3], "outer-end");
}

// ---------------------------------------------------------------------------
// Cancellation.
//
// Rounds abort mid-sequence: the player dies, or Double or Nothing restarts.
// Godot gets this by freeing nodes; here it is explicit.
// ---------------------------------------------------------------------------

namespace {

Task MarkBeforeAndAfter(Log& log) {
    log.push_back("before");
    co_await Delay(0.5f);
    log.push_back("after");        // must never run once cancelled
}

}  // namespace

TEST(SeqCancel, CancelledSequenceDoesNotContinue) {
    Scheduler s;
    Log log;
    s.Spawn(MarkBeforeAndAfter(log));
    s.Tick(0.1f);
    s.CancelAll();
    for (int i = 0; i < 100; ++i) s.Tick(0.016f);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "before");
}

TEST(SeqCancel, CancelDrainsEverything) {
    Scheduler s;
    s.Spawn(JustWait(1.0f));
    s.Spawn(JustWait(2.0f));
    EXPECT_EQ(s.PendingCount(), 2u);
    s.CancelAll();
    EXPECT_EQ(s.PendingCount(), 0u);
    EXPECT_EQ(s.LiveTaskCount(), 0u);
}

TEST(SeqCancel, TasksSpawnedAfterCancelStillRun) {
    // A new round must work after the previous one was aborted.
    Scheduler s;
    s.CancelAll();
    int n = 0;
    s.Spawn(SetAfter(n, 1, 0.1f));
    for (int i = 0; i < 20; ++i) s.Tick(0.016f);
    EXPECT_EQ(n, 1);
}

TEST(SeqCancel, GenerationAdvancesOnEachCancel) {
    Scheduler s;
    const auto g0 = s.Generation();
    s.CancelAll();
    EXPECT_NE(s.Generation(), g0);
    const auto g1 = s.Generation();
    s.CancelAll();
    EXPECT_NE(s.Generation(), g1);
}

TEST(SeqCancel, CancelDuringTickIsSafe) {
    // A sequence can end the round from inside its own body (the shot that
    // kills the player), which cancels the very list being iterated.
    Scheduler s;
    Log log;
    s.Spawn(LogAfter(log, "doomed", 0.2f));

    struct Killer {
        static Task Run(Scheduler& sched, Log& out) {
            co_await Delay(0.1f);
            out.push_back("kill");
            sched.CancelAll();
        }
    };
    s.Spawn(Killer::Run(s, log));

    for (int i = 0; i < 40; ++i) s.Tick(0.016f);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "kill");        // "doomed" was cancelled before firing
    EXPECT_EQ(s.PendingCount(), 0u);
}

// ---------------------------------------------------------------------------
// Time scale and pause.
//
// TimeScaleManager.gd exists in the source, and create_timer(t, false) is
// pause-aware. Sequence timing drifts from the original unless both are
// modelled, so Delay honours scale and pause while DelayUnscaled ignores both.
// ---------------------------------------------------------------------------

namespace {

Task SetAfterUnscaled(int& out, int value, float seconds) {
    co_await DelayUnscaled(seconds);
    out = value;
}

}  // namespace

TEST(SeqTime, HalfSpeedTakesTwiceAsLong) {
    Scheduler s;
    s.SetTimeScale(0.5f);
    int n = 0;
    s.Spawn(SetAfter(n, 1, 1.0f));
    for (int i = 0; i < 70; ++i) s.Tick(0.016f);   // 1.12 s real = 0.56 s scaled
    EXPECT_EQ(n, 0);                               // still short of 1.0 scaled
    for (int i = 0; i < 70; ++i) s.Tick(0.016f);   // 2.24 s real = 1.12 s scaled
    EXPECT_EQ(n, 1);
}

TEST(SeqTime, DoubleSpeedTakesHalfAsLong) {
    Scheduler s;
    s.SetTimeScale(2.0f);
    int n = 0;
    s.Spawn(SetAfter(n, 1, 1.0f));
    for (int i = 0; i < 34; ++i) s.Tick(0.016f);   // ~0.54 s real = ~1.09 scaled
    EXPECT_EQ(n, 1);
}

TEST(SeqTime, UnscaledIgnoresTimeScale) {
    Scheduler s;
    s.SetTimeScale(0.1f);
    int n = 0;
    s.Spawn(SetAfterUnscaled(n, 1, 1.0f));
    for (int i = 0; i < 70; ++i) s.Tick(0.016f);   // ~1.12 s real
    EXPECT_EQ(n, 1);
}

TEST(SeqTime, PauseFreezesScaledDelaysOnly) {
    Scheduler s;
    int scaled = 0, unscaled = 0;
    s.Spawn(SetAfter(scaled, 1, 0.5f));
    s.Spawn(SetAfterUnscaled(unscaled, 1, 0.5f));

    s.SetPaused(true);
    for (int i = 0; i < 60; ++i) s.Tick(0.016f);
    EXPECT_EQ(scaled, 0);        // frozen
    EXPECT_EQ(unscaled, 1);      // UI keeps running

    s.SetPaused(false);
    for (int i = 0; i < 40; ++i) s.Tick(0.016f);
    EXPECT_EQ(scaled, 1);        // resumes exactly where it left off
}

TEST(SeqTime, ZeroTimeScaleFreezesWithoutStalling) {
    Scheduler s;
    s.SetTimeScale(0.0f);
    int n = 0;
    s.Spawn(SetAfter(n, 1, 0.1f));
    for (int i = 0; i < 60; ++i) s.Tick(0.016f);
    EXPECT_EQ(n, 0);
    s.SetTimeScale(1.0f);
    for (int i = 0; i < 20; ++i) s.Tick(0.016f);
    EXPECT_EQ(n, 1);
}

TEST(SeqTime, NegativeTimeScaleIsClampedNotHonoured) {
    // Time must never run backwards; a negative scale would make delays grow.
    Scheduler s;
    s.SetTimeScale(-5.0f);
    EXPECT_GE(s.TimeScale(), 0.0f);
}

// ---------------------------------------------------------------------------
// Awaiting one sequence from another: `co_await Child()` <=> `await Child()`.
// ---------------------------------------------------------------------------
namespace {

Task OuterAwaitingInner(Log& log) {
    log.push_back("outer-start");
    co_await InnerLogged(log);
    log.push_back("outer-end");
}

Task OuterAwaitingInstant(Log& log) {
    log.push_back("outer-start");
    co_await SetNowLogged(log);
    log.push_back("outer-end");
}

Task AwaitThenWait(int& out, float childSeconds, float ownSeconds) {
    co_await JustWait(childSeconds);
    co_await Delay(ownSeconds);
    out = 1;
}

Task Level3(Log& log, std::string tag) {
    co_await Delay(0.1f);
    log.push_back(tag);
}

Task Level2(Log& log) {
    co_await Level3(log, "deepest");
    log.push_back("middle");
}

Task Level1(Log& log) {
    co_await Level2(log);
    log.push_back("top");
}

}  // namespace

TEST(SeqAwait, TheAwaitingSequenceWaitsForTheChildToFinish) {
    Scheduler s;
    Log log;
    s.Spawn(OuterAwaitingInner(log));
    EXPECT_EQ(log, (Log{"outer-start", "inner-start"}))
        << "the child did not start in the same frame it was awaited";

    s.Tick(0.05f);
    EXPECT_EQ(log.size(), 2u) << "the parent ran on before its child finished";

    s.Tick(0.06f);
    EXPECT_EQ(log, (Log{"outer-start", "inner-start", "inner-end", "outer-end"}))
        << "the parent must resume in the SAME tick its child ends, as in Godot";
    s.Tick(0.0f);
    EXPECT_EQ(s.LiveTaskCount(), 0u);
}

TEST(SeqAwait, AChildThatNeverSuspendsDoesNotCostAFrame) {
    Scheduler s;
    Log log;
    s.Spawn(OuterAwaitingInstant(log));
    EXPECT_EQ(log, (Log{"outer-start", "now", "outer-end"}));
    EXPECT_EQ(s.LiveTaskCount(), 0u);
}

TEST(SeqAwait, AwaitedTimeAddsToTheParentsOwnDelays) {
    Scheduler s;
    int done = 0;
    // Binary-exact steps: 0.1f summed five times is not 0.5, and the test
    // would be measuring float rounding instead of the scheduler.
    s.Spawn(AwaitThenWait(done, 0.5f, 0.25f));
    for (int i = 0; i < 5; ++i) s.Tick(0.125f);   // 0.625 s: child done, own delay running
    EXPECT_EQ(done, 0);
    s.Tick(0.125f);                               // 0.75 s = 0.5 + 0.25
    EXPECT_EQ(done, 1);
}

TEST(SeqAwait, NestedAwaitsUnwindInOrderAndKeepTheirParameters) {
    Scheduler s;
    Log log;
    s.Spawn(Level1(log));
    s.Tick(0.2f);
    EXPECT_EQ(log, (Log{"deepest", "middle", "top"}));
}

TEST(SeqAwait, CancellingWhileAChildIsPendingFreesBothFrames) {
    Scheduler s;
    Log log;
    s.Spawn(OuterAwaitingInner(log));
    s.CancelAll();
    for (int i = 0; i < 20; ++i) s.Tick(0.05f);
    EXPECT_EQ(log, (Log{"outer-start", "inner-start"}));
    EXPECT_EQ(s.LiveTaskCount(), 0u);
    EXPECT_EQ(s.PendingCount(), 0u);
}
