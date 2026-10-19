// The Double or Nothing leaderboard is local. Each cashed out run is appended
// to SaveStore entry "board" and ranked as Steam ranks v2.2's uploads: by
// val2compressed of the shown score, earlier runs first on a tie.
#include "game/local_board.h"
#include "game/save_store.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {

using game::BoardRun;
using game::LocalBoard;

std::string FreshDir(const char* name) {
    const std::string dir = std::string(BSHT_BINARY_DIR) + "/board_test_" + name;
    std::remove((dir + "/board.sav").c_str());
    std::remove((dir + "/board.tmp").c_str());
    return dir;
}

// v2.2 LeaderboardManager.val2compressed: below 2^17 a value is itself; above,
// 17 bits of mantissa (values computed from the .gd's arithmetic, in doubles).
TEST(LocalBoard, CompressedScoreKeepsSeventeenBits) {
    EXPECT_EQ(game::CompressedScore(62000), 62000);
    EXPECT_EQ(game::CompressedScore(131072), 131072);
    EXPECT_EQ(game::CompressedScore(248000), 189536);
    EXPECT_EQ(game::CompressedScore(1000000), 403528);
    EXPECT_EQ(game::CompressedScore(int64_t{1} << 31), 1835016);
    EXPECT_EQ(game::CompressedScore(0), 0);
}

// v2.2 UploadScore: the key is val2compressed(initial doubled per round after
// the first), the shown score, not initial x rounds.
TEST(LocalBoard, TheRankScoreIsTheCompressedShownScore) {
    EXPECT_EQ(game::RankScore(BoardRun{"A", 62000, 3}), 189536);
    // v1.2's key ranked these the other way round (5000 < 6000).
    EXPECT_GT(game::RankScore(BoardRun{"LONG", 1000, 5}), game::RankScore(BoardRun{"BIG", 6000, 1}));
}

// LeaderboardManager.gd UpdateDisplay: the score SHOWN is the initial score
// doubled once per round beaten after the first.
TEST(LocalBoard, TheShownScoreDoublesPerRoundAfterTheFirst) {
    EXPECT_EQ(game::ShownScore(BoardRun{"A", 62000, 1}), 62000);
    EXPECT_EQ(game::ShownScore(BoardRun{"A", 62000, 3}), 248000);
    EXPECT_EQ(game::ShownScore(BoardRun{"A", 10, 0}), 10);
}

TEST(LocalBoard, AnEmptyBoardHasNoRuns) {
    const game::SaveStore store(FreshDir("empty"));
    LocalBoard b;
    b.Load(&store);
    EXPECT_TRUE(b.Runs().empty());
    b.Load(nullptr);
    EXPECT_TRUE(b.Runs().empty());
}

TEST(LocalBoard, RunsPersistAndRankBestFirst) {
    const game::SaveStore store(FreshDir("rank"));
    LocalBoard b;
    ASSERT_TRUE(b.Append(&store, BoardRun{"FIRST", 1000, 1}));
    ASSERT_TRUE(b.Append(&store, BoardRun{"BEST", 50000, 2}));
    ASSERT_TRUE(b.Append(&store, BoardRun{"TIE", 1000, 1}));
    LocalBoard again;
    again.Load(&store);
    ASSERT_EQ(again.Runs().size(), 3u);
    EXPECT_EQ(again.Runs()[0].name, "BEST");
    EXPECT_EQ(again.Runs()[1].name, "FIRST") << "a tie keeps the earlier run first";
    EXPECT_EQ(again.Runs()[2].name, "TIE");
    EXPECT_EQ(again.Runs()[0].initial, 50000);
    EXPECT_EQ(again.Runs()[0].beaten, 2);
}

TEST(LocalBoard, TheBoardKeepsItsBestHundred) {
    const game::SaveStore store(FreshDir("cap"));
    LocalBoard b;
    for (int i = 0; i < 105; ++i) ASSERT_TRUE(b.Append(&store, BoardRun{"R" + std::to_string(i), 100 + i, 1}));
    LocalBoard again;
    again.Load(&store);
    ASSERT_EQ(again.Runs().size(), LocalBoard::kMaxRuns);
    EXPECT_EQ(again.Runs().front().name, "R104");
    EXPECT_EQ(again.Runs().back().name, "R5");
}

// The overview's "you" when no waiver is signed yet: whoever cashed out last.
TEST(LocalBoard, RemembersWhoUploadedLast) {
    const game::SaveStore store(FreshDir("latest"));
    LocalBoard b;
    ASSERT_TRUE(b.Append(&store, BoardRun{"BEST", 90000, 3}));
    ASSERT_TRUE(b.Append(&store, BoardRun{"LAST", 10, 1}));
    LocalBoard again;
    again.Load(&store);
    EXPECT_EQ(again.Latest(), "LAST");
}

TEST(LocalBoard, WithoutAStoreNothingIsWritten) {
    LocalBoard b;
    EXPECT_FALSE(b.Append(nullptr, BoardRun{"X", 1, 1}));
}

// v2.2's waiver stores " " + name; the board shows "#1 ABE", not "#1  ABE".
TEST(LocalBoard, AWaiverNameKeepsNoLeadingSpace) {
    const game::SaveStore store(FreshDir("space"));
    LocalBoard b;
    ASSERT_TRUE(b.Append(&store, BoardRun{" ABE", 100, 2}));
    LocalBoard again;
    again.Load(&store);
    ASSERT_EQ(again.Runs().size(), 1u);
    EXPECT_EQ(again.Runs()[0].name, "ABE");
    EXPECT_EQ(again.Latest(), "ABE");
}

// A run saved before v2.2 ("ABE") and one after (" ABE") are the same player.
TEST(LocalBoard, OldAndNewNamesAreOneName) {
    EXPECT_EQ(game::BoardName(" ABE"), "ABE");
    EXPECT_EQ(game::BoardName("ABE  "), "ABE");
    EXPECT_EQ(game::BoardName("ABE"), "ABE");
    EXPECT_EQ(game::BoardName("   "), "");
}

TEST(LocalBoard, ShownScoreStopsAtTheLargestInt) {
    EXPECT_EQ(game::ShownScore(BoardRun{"A", int64_t{1} << 20, 200}), INT64_MAX);
    EXPECT_EQ(game::ShownScore(BoardRun{"A", 3, 64}), INT64_MAX);
    EXPECT_EQ(game::ShownScore(BoardRun{"A", 3, 62}), int64_t{3} << 61);
}

TEST(LocalBoard, CompressedScoreOfNothingIsNothing) {
    EXPECT_EQ(game::CompressedScore(0), 0);
    EXPECT_EQ(game::CompressedScore(-5), 0);
    EXPECT_GT(game::CompressedScore(INT64_MAX), 0);
}

}  // namespace
