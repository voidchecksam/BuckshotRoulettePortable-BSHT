// Achievements, kept locally. The game's own scripts unlock them through
// Achievement.UnlockAchievement, which called Steam; the port's Achievement
// records the id in a SaveStore and queues a toast the first time. The call
// sites are translated 1:1 and tested here against their .gd lines.
#include "game/achievements.h"
#include "game/save_store.h"
#include "scripts/GlobalVariables.h"

#include "godot/node.h"
#include "godot/scene_tree.h"
#include "scripts/AchievementManager.h"
#include "scripts/DeathManager.h"
#include "scripts/ItemInteraction.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/ShotgunShooting.h"

#include <cstdio>
#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace {

const std::string kText = std::string(BSHT_SOURCE_DIR) + "/tools/achievements_en.json";

std::string FreshDir(const char* name) {
    const std::string dir = std::string(BSHT_BINARY_DIR) + "/ach_test_" + name;
    std::remove((dir + "/achievements.sav").c_str());
    std::remove((dir + "/achievements.tmp").c_str());
    return dir;
}

TEST(Achievements, LoadsTheSixteenInOrder) {
    game::Achievements a;
    ASSERT_TRUE(a.LoadText(kText.c_str()));
    ASSERT_EQ(a.All().size(), 16u);
    for (size_t i = 0; i < 16; ++i) EXPECT_EQ(a.All()[i].id, "ach" + std::to_string(i + 1));
    EXPECT_EQ(a.All()[7].name, "Coin Flip");
    EXPECT_EQ(a.All()[10].description, "Enter the name \"GOD\" when signing the waiver.");
}

TEST(Achievements, UnlockIsTrueOnceAndQueuesOneToast) {
    const game::SaveStore store(FreshDir("once"));
    game::Achievements a;
    ASSERT_TRUE(a.LoadText(kText.c_str()));
    a.Attach(&store);
    EXPECT_FALSE(a.IsUnlocked("ach8"));
    EXPECT_TRUE(a.Unlock("ach8"));
    EXPECT_FALSE(a.Unlock("ach8"));
    EXPECT_TRUE(a.IsUnlocked("ach8"));
    game::AchievementText toast;
    ASSERT_TRUE(a.PopToast(toast));
    EXPECT_EQ(toast.name, "Coin Flip");
    EXPECT_FALSE(a.PopToast(toast));
}

TEST(Achievements, PersistAcrossANewInstance) {
    const game::SaveStore store(FreshDir("persist"));
    {
        game::Achievements a;
        ASSERT_TRUE(a.LoadText(kText.c_str()));
        a.Attach(&store);
        ASSERT_TRUE(a.Unlock("ach15"));
    }
    game::Achievements b;
    ASSERT_TRUE(b.LoadText(kText.c_str()));
    b.Attach(&store);
    EXPECT_TRUE(b.IsUnlocked("ach15"));
    EXPECT_FALSE(b.Unlock("ach15"));
    game::AchievementText toast;
    EXPECT_FALSE(b.PopToast(toast));  // no toast for an old unlock
}

TEST(Achievements, UnknownIdIsRefused) {
    game::Achievements a;
    ASSERT_TRUE(a.LoadText(kText.c_str()));
    EXPECT_FALSE(a.Unlock("ach99"));
    EXPECT_FALSE(a.IsUnlocked("ach99"));
}

// The scripts, on the smallest tree their checks read.
template <class T>
T* Attach(godot::SceneTree& t, godot::Node* parent, const char* name) {
    godot::Node* node = t.add_node(std::make_unique<godot::Node>(name), parent);
    auto script = std::make_unique<T>();
    T* raw = script.get();
    node->set_script(std::move(script));
    return raw;
}

struct ScriptChecks : ::testing::Test {
    godot::SceneTree t;
    game::SaveStore store{FreshDir("scripts")};
    game::Achievements achievements;
    scripts::Achievement* ach = nullptr;
    scripts::RoundManager* rm = nullptr;
    scripts::ShellSpawner* spawner = nullptr;
    scripts::ShotgunShooting* shooting = nullptr;

    void SetUp() override {
        ASSERT_TRUE(achievements.LoadText(kText.c_str()));
        achievements.Attach(&store);
        godot::Node* root = t.add_node(std::make_unique<godot::Node>("main"), nullptr);
        ach = Attach<scripts::Achievement>(t, root, "achievement manager");
        ach->store = &achievements;
        rm = Attach<scripts::RoundManager>(t, root, "round manager");
        spawner = Attach<scripts::ShellSpawner>(t, root, "shell spawner");
        shooting = Attach<scripts::ShotgunShooting>(t, root, "shotgun shooting");
        shooting->roundManager = rm;
        shooting->shellSpawner = spawner;
        shooting->ach = ach;
    }
};

TEST_F(ScriptChecks, CoinFlipAtEqualLiveAndBlankWithoutInfo) {  // gd: L194-L197
    spawner->sequenceArray = {"live", "blank", "live", "blank"};
    rm->playerCurrentTurnItemArray = {"beer"};
    shooting->CheckAchievement_coinflip();
    EXPECT_TRUE(achievements.IsUnlocked("ach8"));
}

TEST_F(ScriptChecks, CoinFlipRefusedWithTheMagnifierOrUnequalShells) {
    spawner->sequenceArray = {"live", "blank", "live", "blank"};
    rm->playerCurrentTurnItemArray = {"magnifying glass"};
    shooting->CheckAchievement_coinflip();
    EXPECT_FALSE(achievements.IsUnlocked("ach8"));
    rm->playerCurrentTurnItemArray = {};
    spawner->sequenceArray = {"live", "blank", "blank"};
    shooting->CheckAchievement_coinflip();
    EXPECT_FALSE(achievements.IsUnlocked("ach8"));
}

TEST_F(ScriptChecks, WhyAndStyleReadTheTurnItems) {  // gd: L199-L203
    rm->playerCurrentTurnItemArray = {"cigarettes"};
    shooting->CheckAchievement_why();
    shooting->CheckAchievement_style();
    EXPECT_FALSE(achievements.IsUnlocked("ach13"));
    EXPECT_FALSE(achievements.IsUnlocked("ach15"));
    rm->playerCurrentTurnItemArray = {"magnifying glass", "handsaw"};
    shooting->CheckAchievement_why();
    shooting->CheckAchievement_style();
    EXPECT_TRUE(achievements.IsUnlocked("ach13"));
    EXPECT_TRUE(achievements.IsUnlocked("ach15"));
}

TEST_F(ScriptChecks, KoniAndFullHouse) {  // ItemInteraction gd: L186-L192
    auto* interaction = Attach<scripts::ItemInteraction>(t, t.get_root(), "item interaction");
    interaction->roundManager = rm;
    interaction->ach = ach;
    rm->playerCurrentTurnItemArray = {"cigarettes", "beer"};
    interaction->CheckAchievement_koni();
    EXPECT_FALSE(achievements.IsUnlocked("ach9"));
    rm->playerCurrentTurnItemArray.push_back("expired medicine");
    interaction->CheckAchievement_koni();
    EXPECT_TRUE(achievements.IsUnlocked("ach9"));
    // Full house compares SIZES only (gd: L192), as the original does.
    interaction->CheckAchievement_full();
    EXPECT_FALSE(achievements.IsUnlocked("ach12"));
    rm->playerCurrentTurnItemArray = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    interaction->CheckAchievement_full();
    EXPECT_TRUE(achievements.IsUnlocked("ach12"));
}

TEST_F(ScriptChecks, UnlockWithNoStoreIsHarmless) {
    ach->store = nullptr;
    ach->UnlockAchievement("ach1");
    EXPECT_FALSE(achievements.IsUnlocked("ach1"));
}

TEST_F(ScriptChecks, KnowWhenToQuitOnAnEndlessDeathOverAMillion) {  // DeathManager gd: L143
    auto* death = Attach<scripts::DeathManager>(t, t.get_root(), "death manager");
    death->shotgunShooting = shooting;
    death->rm = rm;
    death->ach = ach;
    rm->endless = true;
    rm->endscore = 1000001;
    t.spawn(death->MainDeathRoutine());
    EXPECT_TRUE(achievements.IsUnlocked("ach10"));
}

}  // namespace

namespace {

// v2.2 AchievementManager.gd: the unlock reaches the store only while
// using_steam (true in the port, which keeps its local achievements).
TEST_F(ScriptChecks, AnAchievementUnlocksOnlyWhileUsingSteam) {  // gd: L3-L6
    const bool saved = scripts::GlobalVariables::using_steam;
    scripts::GlobalVariables::using_steam = false;
    ach->UnlockAchievement("ach1");
    EXPECT_FALSE(achievements.IsUnlocked("ach1"));
    scripts::GlobalVariables::using_steam = true;
    ach->UnlockAchievement("ach1");
    EXPECT_TRUE(achievements.IsUnlocked("ach1"));
    scripts::GlobalVariables::using_steam = saved;
}

}  // namespace
