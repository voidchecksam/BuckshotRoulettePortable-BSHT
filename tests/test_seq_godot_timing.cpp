// SceneTreeTimer counts in double (scene/main/scene_tree.cpp:570 at
// 4.1.4-stable: `double time_left ...; time_left -= p_delta; if (time_left <= 0)`).
// Counted in float, 1/60 rounds the other way and a timer fires a frame early
// or late (every beat of parity_ceremony_b1r1 would drift by one frame). These
// tick counts are Godot's, at 60 fps.
#include <gtest/gtest.h>

#include "seq/delay.h"
#include "seq/scheduler.h"
#include "seq/task.h"

namespace {

seq::Task WaitThenMark(double seconds, bool* done) {
    co_await seq::Delay(seconds);
    *done = true;
}

int TicksUntilResumed(double seconds) {
    seq::Scheduler s;
    bool done = false;
    s.Spawn(WaitThenMark(seconds, &done));
    int ticks = 0;
    while (!done && ticks < 100000) {
        s.Tick(1.0 / 60.0);
        ++ticks;
    }
    return ticks;
}

}  // namespace

TEST(SeqGodotTiming, TimersCountInDoubleLikeSceneTreeTimer) {
    EXPECT_EQ(TicksUntilResumed(0.5), 31);
    EXPECT_EQ(TicksUntilResumed(0.8), 48);
    EXPECT_EQ(TicksUntilResumed(1.4), 84);
    EXPECT_EQ(TicksUntilResumed(0.45), 28);
    EXPECT_EQ(TicksUntilResumed(1.0), 60);
    EXPECT_EQ(TicksUntilResumed(2.3), 139);
}
