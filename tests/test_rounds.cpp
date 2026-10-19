// RoundTable parsing.
//
// Driven from an inline document so the suite is green on a fresh clone where
// extraction has not been run, plus a smoke test against the real extracted
// file that skips cleanly when it is absent.
#include <gtest/gtest.h>

#include "core/round_table.h"

#include <string>

using namespace core;

namespace {

const char* kJson = R"({
  "rounds": [
    {"roundIndex":0,"startingHealth":2,"amountLive":1,"amountBlank":2,
     "usingItems":false,"shufflingArray":false,"insertingInRandomOrder":true,
     "numberOfItemsToGrab":0,"showingIndicator":true,"indicatorNumber":0,
     "sequenceHidden":false,"bootingUpCounter":true,"isFirstRound":true,
     "hasIntroductoryText":false,"hasIntro2":false},
    {"roundIndex":1,"startingHealth":0,"amountLive":3,"amountBlank":2,
     "usingItems":true,"shufflingArray":true,"insertingInRandomOrder":true,
     "numberOfItemsToGrab":2,"showingIndicator":true,"indicatorNumber":2,
     "sequenceHidden":true,"bootingUpCounter":false,"isFirstRound":false,
     "hasIntroductoryText":false,"hasIntro2":true}
  ],
  "batches": [
    {"batchIndex":0,"roundIndices":[0,1,2,3,4]},
    {"batchIndex":1,"roundIndices":[0,0,1,0,0]}
  ],
  "items": [
    {"itemName":"beer","amount_active":0,"amount_main":8,"amount_don":2},
    {"itemName":"inverter","amount_active":0,"amount_main":0,"amount_don":8}
  ]
})";

}  // namespace

TEST(RoundTable, ParsesFirstRound) {
    RoundTable t;
    ASSERT_TRUE(t.LoadFromString(kJson));
    ASSERT_EQ(t.RoundCount(), 2u);
    EXPECT_EQ(t.Round(0).startingHealth, 2);
    EXPECT_EQ(t.Round(0).amountLive, 1);
    EXPECT_EQ(t.Round(0).amountBlank, 2);
    EXPECT_EQ(t.Round(0).ShellCount(), 3);
    EXPECT_TRUE (t.Round(0).isFirstRound);
    EXPECT_TRUE (t.Round(0).bootingUpCounter);
    EXPECT_FALSE(t.Round(0).usingItems);
    EXPECT_FALSE(t.Round(0).shufflingArray);
}

TEST(RoundTable, ParsesSecondRoundAndItsFlags) {
    RoundTable t;
    ASSERT_TRUE(t.LoadFromString(kJson));
    EXPECT_EQ(t.Round(1).amountLive, 3);
    EXPECT_EQ(t.Round(1).numberOfItemsToGrab, 2);
    EXPECT_TRUE(t.Round(1).usingItems);
    EXPECT_TRUE(t.Round(1).insertingInRandomOrder);
    EXPECT_TRUE(t.Round(1).sequenceHidden);
    EXPECT_TRUE(t.Round(1).hasIntro2);
    // 0 means "carry over from the previous round", not "start dead".
    EXPECT_EQ(t.Round(1).startingHealth, 0);
}

TEST(RoundTable, ParsesBatchesIncludingRepeatedRoundIndices) {
    // roundIndex is not unique across batches; anything keyed on it alone is
    // wrong. Batch 1 really is [0,0,1,0,0] in the extracted data.
    RoundTable t;
    ASSERT_TRUE(t.LoadFromString(kJson));
    ASSERT_EQ(t.BatchCount(), 2u);
    EXPECT_EQ(t.Batch(0).batchIndex, 0);
    ASSERT_EQ(t.Batch(0).roundIndices.size(), 5u);
    EXPECT_EQ(t.Batch(0).roundIndices[4], 4);
    ASSERT_EQ(t.Batch(1).roundIndices.size(), 5u);
    EXPECT_EQ(t.Batch(1).roundIndices[0], 0);
    EXPECT_EQ(t.Batch(1).roundIndices[2], 1);
}

TEST(RoundTable, ParsesItemPool) {
    RoundTable t;
    ASSERT_TRUE(t.LoadFromString(kJson));
    ASSERT_EQ(t.Items().size(), 2u);
    EXPECT_EQ(t.Items()[0].itemName, "beer");
    EXPECT_EQ(t.Items()[0].amount_main, 8);
    EXPECT_EQ(t.Items()[0].amount_don, 2);
}

TEST(RoundTable, IdentifiesDoubleOrNothingExclusiveItems) {
    RoundTable t;
    ASSERT_TRUE(t.LoadFromString(kJson));
    EXPECT_FALSE(t.Items()[0].DoubleOrNothingOnly());   // beer
    EXPECT_TRUE (t.Items()[1].DoubleOrNothingOnly());   // inverter
}

TEST(RoundTable, PoolWeightSwitchesOnMode) {
    RoundTable t;
    ASSERT_TRUE(t.LoadFromString(kJson));
    EXPECT_EQ(t.PoolWeight("beer", false), 8);
    EXPECT_EQ(t.PoolWeight("beer", true), 2);
    EXPECT_EQ(t.PoolWeight("inverter", false), 0);      // never in the base game
    EXPECT_EQ(t.PoolWeight("inverter", true), 8);
    EXPECT_EQ(t.PoolWeight("shotgun", false), 0);       // unknown item
}

TEST(RoundTable, RejectsMalformedInput) {
    RoundTable t;
    EXPECT_FALSE(t.LoadFromString("{ this is not json"));
    EXPECT_EQ(t.RoundCount(), 0u);
    EXPECT_FALSE(t.LoadFromString("{}"));
    EXPECT_FALSE(t.LoadFromString(nullptr));
    EXPECT_FALSE(t.LoadFromString(R"({"rounds":[]})"));
}

TEST(RoundTable, OutOfRangeAccessIsClampedNotUndefined) {
    RoundTable t;
    ASSERT_TRUE(t.LoadFromString(kJson));
    EXPECT_EQ(t.Round(999).roundIndex, t.Round(t.RoundCount() - 1).roundIndex);
    EXPECT_EQ(t.Batch(999).batchIndex, t.Batch(t.BatchCount() - 1).batchIndex);
}

TEST(RoundTable, EmptyTableAccessIsSafe) {
    RoundTable t;
    EXPECT_EQ(t.RoundCount(), 0u);
    EXPECT_EQ(t.Round(0).amountLive, 0);       // must not crash
    EXPECT_EQ(t.Batch(0).batchIndex, 0);
}

TEST(RoundTable, MissingFileIsReported) {
    RoundTable t;
    EXPECT_FALSE(t.LoadFromFile("assets/extracted/does_not_exist.json"));
    EXPECT_FALSE(t.LoadFromFile(nullptr));
}

TEST(RoundTable, LoadsTheRealExtractedTable) {
    // Absolute path: ctest runs with the build dir as CWD, so a relative path
    // would make this skip silently instead of verifying anything.
    const std::string path =
        std::string(BSHT_SOURCE_DIR) + "/assets/extracted/round_table.json";
    RoundTable t;
    if (!t.LoadFromFile(path.c_str())) {
        GTEST_SKIP() << "run tools/godot/export_round_table.gd first";
    }
    // The real game: 15 rounds in 3 batches of 5, all 9 items.
    EXPECT_EQ(t.RoundCount(), 15u);
    EXPECT_EQ(t.BatchCount(), 3u);
    EXPECT_EQ(t.Items().size(), 9u);

    // Three-act structure: starting health 2 -> 4 -> 6.
    EXPECT_EQ(t.Round(0).startingHealth, 2);
    EXPECT_EQ(t.Round(0).amountLive, 1);
    EXPECT_EQ(t.Round(0).amountBlank, 2);

    // Exactly four items are Double-or-Nothing exclusive.
    int donOnly = 0;
    for (const auto& e : t.Items()) {
        if (e.DoubleOrNothingOnly()) ++donOnly;
    }
    EXPECT_EQ(donOnly, 4);

    // No round may have an empty chamber.
    for (std::size_t i = 0; i < t.RoundCount(); ++i) {
        EXPECT_GT(t.Round(i).ShellCount(), 0) << "round " << i;
    }
}
