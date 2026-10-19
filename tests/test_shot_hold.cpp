// app/shot_hold: a scenario shot on the device is read after the frame has been
// redrawn for a while: Vita3K copies a rendered surface back to emulated memory
// late, and a read on the shot's own frame comes back all zeros.
#include "app/shot_hold.h"

#include <gtest/gtest.h>

namespace {

TEST(ShotHold, WithoutAHoldTheShotIsTakenOnItsFrame) {
    app::ShotHold hold(0);

    EXPECT_EQ(hold.Request(1480), 1480);
    EXPECT_FALSE(hold.Holding());
}

TEST(ShotHold, AHeldShotIsTakenOnTheLastHeldFrame) {
    app::ShotHold hold(3);

    EXPECT_EQ(hold.Request(1480), -1);
    ASSERT_TRUE(hold.Holding());
    EXPECT_EQ(hold.Tick(), -1);
    EXPECT_EQ(hold.Tick(), -1);
    EXPECT_EQ(hold.Tick(), 1480);
    EXPECT_FALSE(hold.Holding());
}

TEST(ShotHold, AFrameWithoutAShotNeverHolds) {
    app::ShotHold hold(3);

    EXPECT_EQ(hold.Request(-1), -1);
    EXPECT_FALSE(hold.Holding());
}

}  // namespace

// A fixed hold races the window grab, which can take 1-2.5 s. With a handshake
// the frame is held until the grabber says it has it (released), at least the
// minimum frames, at most the maximum.
namespace {

TEST(ShotHold, AHandshakeHoldsUntilReleasedAfterTheMinimum) {
    app::ShotHold hold(/*minFrames=*/2, /*maxFrames=*/10);
    ASSERT_EQ(hold.Request(1480), -1);
    EXPECT_EQ(hold.Held(), 1480);

    EXPECT_EQ(hold.Tick(true), -1);    // released early: the minimum still holds
    EXPECT_EQ(hold.Tick(false), -1);   // minimum done, not released yet
    EXPECT_EQ(hold.Tick(false), -1);
    EXPECT_EQ(hold.Tick(true), 1480);
    EXPECT_FALSE(hold.TimedOut());
    EXPECT_FALSE(hold.Holding());
}

TEST(ShotHold, AHandshakeThatNeverComesTimesOutAndSaysSo) {
    app::ShotHold hold(/*minFrames=*/1, /*maxFrames=*/3);
    hold.Request(700);

    EXPECT_EQ(hold.Tick(false), -1);
    EXPECT_EQ(hold.Tick(false), -1);
    EXPECT_EQ(hold.Tick(false), 700);
    EXPECT_TRUE(hold.TimedOut());
}

}  // namespace
