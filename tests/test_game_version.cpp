#include "extract/game_version.h"

#include <gtest/gtest.h>

TEST(GameVersion, ReadsTheNumberAndHotfix) {
    const char* gd =
        "extends Node\n\nvar currentVersion_nr = \"v2.2.0\"\nvar currentVersion_hotfix = 6\nvar using_steam = true\n";
    const auto v = extract::ParseGameVersion(gd);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->number, "v2.2.0");
    EXPECT_EQ(v->hotfix, 6);
    EXPECT_TRUE(extract::IsSupported(*v));
    EXPECT_EQ(extract::Describe(*v), "v2.2.0 hotfix 6");
}

TEST(GameVersion, AnotherVersionIsRefused) {
    const auto v = extract::ParseGameVersion("var currentVersion_nr = \"v2.3.0\"\nvar currentVersion_hotfix = 1\n");
    ASSERT_TRUE(v.has_value());
    EXPECT_FALSE(extract::IsSupported(*v));
}

TEST(GameVersion, NothingToReadIsNoVersion) {
    EXPECT_FALSE(extract::ParseGameVersion("extends Node\n").has_value());
    EXPECT_FALSE(extract::ParseGameVersion("var currentVersion_nr = \"v2.2.0\"\n").has_value());
}
