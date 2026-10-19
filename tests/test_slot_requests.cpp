// core::SlotRequests: "the latest request per slot wins" for work done off the
// calling thread. platform::Audio loads streamed .ogg files on a loader thread
// with it (reading a 3 MB track on the frame thread froze the Vita for up to
// 1.3 s); a newer PlayStream or a StopStream on the same voice makes an older
// load stale, so it is dropped rather than installed.
#include "core/slot_requests.h"

#include <gtest/gtest.h>

#include <string>

using Requests = core::SlotRequests<std::string>;

TEST(SlotRequests, APostedRequestIsTakenAndCurrent) {
    Requests r(3);
    r.Post(1, "a.ogg");
    Requests::Request got;
    ASSERT_TRUE(r.Take(got));
    EXPECT_EQ(got.slot, 1);
    EXPECT_EQ(got.payload, "a.ogg");
    EXPECT_TRUE(r.IsCurrent(got));
    EXPECT_FALSE(r.Take(got)) << "the queue is empty";
}

TEST(SlotRequests, ANewerRequestOnTheSameSlotSupersedesTheOlder) {
    Requests r(3);
    r.Post(0, "old.ogg");
    r.Post(0, "new.ogg");
    Requests::Request got;
    ASSERT_TRUE(r.Take(got));
    EXPECT_EQ(got.payload, "new.ogg") << "the stale one is skipped, not loaded";
    EXPECT_FALSE(r.Take(got));
}

TEST(SlotRequests, OtherSlotsAreUntouched) {
    Requests r(3);
    r.Post(0, "music.ogg");
    r.Post(2, "hum.ogg");
    Requests::Request a, b;
    ASSERT_TRUE(r.Take(a));
    ASSERT_TRUE(r.Take(b));
    EXPECT_EQ(a.payload, "music.ogg");
    EXPECT_EQ(b.payload, "hum.ogg");
}

TEST(SlotRequests, ACancelDropsAQueuedRequest) {
    Requests r(3);
    r.Post(1, "a.ogg");
    r.Cancel(1);
    Requests::Request got;
    EXPECT_FALSE(r.Take(got));
}

TEST(SlotRequests, ARequestTakenThenSupersededIsNoLongerCurrent) {
    // The loader took it and is reading the file when the game posts again.
    Requests r(3);
    r.Post(0, "a.ogg");
    Requests::Request loading;
    ASSERT_TRUE(r.Take(loading));
    r.Post(0, "b.ogg");
    EXPECT_FALSE(r.IsCurrent(loading)) << "installing it would replace the newer track";
    Requests::Request next;
    ASSERT_TRUE(r.Take(next));
    EXPECT_TRUE(r.IsCurrent(next));
}

TEST(SlotRequests, ACancelWhileLoadingMakesTheLoadStale) {
    Requests r(3);
    r.Post(2, "a.ogg");
    Requests::Request loading;
    ASSERT_TRUE(r.Take(loading));
    r.Cancel(2);
    EXPECT_FALSE(r.IsCurrent(loading));
}

TEST(SlotRequests, AnOutOfRangeSlotIsIgnored) {
    Requests r(2);
    r.Post(5, "x.ogg");
    r.Post(-1, "y.ogg");
    r.Cancel(7);
    Requests::Request got;
    EXPECT_FALSE(r.Take(got));
}
