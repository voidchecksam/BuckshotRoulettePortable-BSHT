// The dealer uses items: DealerIntelligence.DealerChoice's item half and
// HandManager. The decisions are checked through what DealerChoice does before
// its first await; the full turns are parity scenarios (dealer_*).
#include "item_fixtures.h"

#include "godot/rng.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/HandManager.h"
#include "scripts/HandcuffManager.h"
#include "scripts/ItemInteraction.h"
#include "scripts/MedicineManager.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/ShellSpawner.h"

#include "godot/material.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace item_fixtures;

namespace {

struct DealerItemTree : ItemTableTree {
    scripts::DealerIntelligence* ai = nullptr;
    scripts::HandManager* hands = nullptr;
    scripts::ShellSpawner* spawner = nullptr;
    godot::Node3D* dealerItem = nullptr;

    void SetUp() override {
        ItemTableTree::SetUp();
        if (!scenesLoaded) return;
        godot::Node* root = t.get_root()->get_child(0);
        godot::Node* managers = root->get_child(0);
        spawner = Attach<scripts::ShellSpawner>(Add<godot::Node>(t, "shell spawner", managers));
        spawner->roundManager = roundManager;
        roundManager->shellSpawner = spawner;
        roundManager->health_opponent = 4;
        rounds[0].startingHealth = 4;

        ai = Attach<scripts::DealerIntelligence>(Add<godot::Node>(t, "dealer intelligence", managers));
        ai->roundManager = roundManager;
        ai->shellSpawner = spawner;
        ai->itemManager = items;
        ai->amounts = amounts;
        ai->camera = items->camera;
        ai->speaker_handCrack = Add<godot::AudioStreamPlayer>(t, "speaker_handcrack", root);
        ai->soundArray_cracks = {"crack1.ogg", "crack2.ogg"};
        auto* ejector = Attach<scripts::ShellEjectManager>(Add<godot::Node>(t, "eject dealer", managers));
        ejector->animator_fader = Add<godot::AnimationPlayer>(t, "fader", root);
        ejector->mat_live = godot::MaterialLibrary::get("res://test/eject live.tres", godot::SurfaceMaterial{});
        ejector->mat_blank = godot::MaterialLibrary::get("res://test/eject blank.tres", godot::SurfaceMaterial{});
        ai->shellEject_dealer = ejector;

        hands = Attach<scripts::HandManager>(Add<godot::Node>(t, "hand manager", managers));
        hands->dealerAI = ai;
        hands->itemManager = items;
        hands->amounts = amounts;
        hands->cam = items->camera;
        hands->animator_hands = Add<godot::AnimationPlayer>(t, "animator_dealer hands", root);
        hands->animator_dealerHeadLook = Add<godot::AnimationPlayer>(t, "animator_head", root);
        hands->handParent_L = Add<godot::Node3D>(t, "hand parent L", root);
        hands->handParent_R = Add<godot::Node3D>(t, "hand parent R", root);
        hands->hand_defaultL = Add<godot::Node3D>(t, "hand default L", root);
        hands->hand_defaultR = Add<godot::Node3D>(t, "hand default R", root);
        hands->gridOffsetArray = std::vector<godot::Vector3>(16);
        hands->lerpDuration = 0.8;
        hands->speaker_interaction = Add<godot::AudioStreamPlayer>(t, "speaker_hands", root);
        ai->hands = hands;

        // One item each: the player's, and the dealer's that the deal places.
        TakeOne();
        Place(0);
        items->GrabItems_Enemy();
        for (int i = 0; i < items->itemSpawnParent->get_child_count(); ++i) {
            auto* n = godot::cast_to<godot::Node3D>(items->itemSpawnParent->get_child(i));
            auto* b = n ? godot::script_cast<scripts::InteractionBranch>(n->get_child(1)) : nullptr;
            if (b && !b->isPlayerSide) dealerItem = n;
        }
    }

    // Every item on the dealer's side becomes `name`: the decisions read only
    // itemName, and the deal may give him more than one.
    void DealerHolds(const std::string& name) {
        for (int i = 0; i < items->itemSpawnParent->get_child_count(); ++i) {
            auto* n = godot::cast_to<godot::Node3D>(items->itemSpawnParent->get_child(i));
            auto* b = n ? godot::script_cast<scripts::InteractionBranch>(n->get_child(1)) : nullptr;
            if (b && !b->isPlayerSide) b->itemName = name;
        }
    }
    void PlayerHolds(const std::string& name) {
        for (int i = 0; i < items->itemSpawnParent->get_child_count(); ++i) {
            auto* n = godot::cast_to<godot::Node3D>(items->itemSpawnParent->get_child(i));
            auto* b = n ? godot::script_cast<scripts::InteractionBranch>(n->get_child(1)) : nullptr;
            if (b && b->isPlayerSide) b->itemName = name;
        }
    }
    void Decide() {
        t.spawn(ai->DealerChoice());
        RunFor(1.0 / 60.0);
    }
};

}  // namespace

TEST_F(DealerItemTree, TheMagnifierTellsTheDealerTheChamber) {
    if (!scenesLoaded || !dealerItem) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live", "blank"};
    DealerHolds("magnifying glass");
    Decide();
    EXPECT_TRUE(ai->dealerKnowsShell);
    EXPECT_EQ(ai->knownShell, "live");
    EXPECT_EQ(ai->dealerTarget, "player");
}

TEST_F(DealerItemTree, TheDealerCuffsAPlayerWhoIsFree) {
    if (!scenesLoaded || !dealerItem) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live", "blank"};
    DealerHolds("handcuffs");
    Decide();
    EXPECT_TRUE(roundManager->playerCuffed);
}

TEST_F(DealerItemTree, TheDealerSawsOnlyWhenHeKnowsTheShellIsLive) {
    if (!scenesLoaded || !dealerItem) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live"};   // one shell: he knows it
    DealerHolds("handsaw");
    Decide();
    EXPECT_TRUE(roundManager->barrelSawedOff);
    EXPECT_EQ(roundManager->currentShotgunDamage, 2);
}

TEST_F(DealerItemTree, TheInverterTurnsAKnownBlankLive) {
    if (!scenesLoaded || !dealerItem) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"blank"};
    DealerHolds("inverter");
    Decide();
    EXPECT_EQ(spawner->sequenceArray[0], "live");
    EXPECT_EQ(ai->dealerTarget, "player");
}

TEST_F(DealerItemTree, ThePhoneMarksOneLaterShellKnown) {
    if (!scenesLoaded || !dealerItem) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live", "blank", "live", "blank"};
    ai->sequenceArray_knownShell = {false, false, false, false};
    DealerHolds("burner phone");
    Decide();
    EXPECT_FALSE(ai->sequenceArray_knownShell[0]);
    EXPECT_EQ(std::count(ai->sequenceArray_knownShell.begin(), ai->sequenceArray_knownShell.end(), true), 1);
}

TEST_F(DealerItemTree, TheDealersHandGoesToTheItemsGrid) {
    if (!scenesLoaded || !dealerItem) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live", "blank"};
    DealerHolds("magnifying glass");   // a right-hand item
    Decide();
    EXPECT_TRUE(hands->moving);
    EXPECT_TRUE(hands->lerping_R);
    EXPECT_FALSE(hands->lerping_L);
}

// DealerChoice gd L240-L262: with adrenaline on his side the dealer counts the
// PLAYER's items as his own; wanting one of them, he first picks up the
// adrenaline, then steals it (HandManager.stealing).
TEST_F(DealerItemTree, AdrenalineLetsTheDealerStealThePlayersHandsaw) {
    if (!scenesLoaded || !dealerItem) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live"};   // one shell: he knows it is live
    DealerHolds("adrenaline");
    PlayerHolds("handsaw");
    Decide();
    EXPECT_TRUE(roundManager->barrelSawedOff);
    EXPECT_EQ(hands->activeItemToGrab, "adrenaline") << "the adrenaline is taken first";
    EXPECT_EQ(ai->inv_playerside, std::vector<std::string>{"handsaw"});
    for (const std::string& own : ai->inv_dealerside) EXPECT_EQ(own, "adrenaline");
}

// v2.2: the dealer's medicine heals below .5 (v1.2: .4). A first draw in
// [.4, .5) is where the two versions disagree.
TEST_F(DealerItemTree, TheDealersMedicineHealsBelowAHalf) {
    if (!scenesLoaded || !dealerItem) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    godot::Node* managers = t.get_root()->get_child(0)->get_child(0);
    ai->medicine = Attach<scripts::Medicine>(Add<godot::Node>(t, "medicine manager", managers));
    ai->medicine->dealerDying = true;
    roundManager->health_opponent = 2;   // hurt, but not at 1
    spawner->sequenceArray = {"live", "blank"};
    DealerHolds("expired medicine");
    uint64_t seed = 1;
    for (;; ++seed) {
        godot::seed(seed);
        const double v = godot::randf_range(0.0, 1.0);
        if (v >= .4 && v < .5) break;
    }
    godot::seed(seed);
    Decide();
    EXPECT_TRUE(ai->usingMedicine);
    EXPECT_FALSE(ai->medicine->dealerDying);
}

TEST_F(DealerItemTree, TheDealersUseAnimationsReachTheirScripts) {
    if (!scenesLoaded || !dealerItem) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    // HandManager's own tracks call into other scripts (dispatchers tested
    // there); HandManager itself is reached by nothing.
    EXPECT_FALSE(hands->call("NoSuchMethod", {}));
}

// `dealer use expired medicine` reaches Medicine only through a METHOD key
// (UseMedicine_Dealer at 0 s, animation_methods.json). An unhandled key is
// dropped silently, so it must go through call().
TEST(DealerItemMethods, UseMedicineDealerHasADispatcher) {
    godot::SceneTree t;
    auto* medicine =
        item_fixtures::Attach<scripts::Medicine>(item_fixtures::Add<godot::Node>(t, "medicine manager", t.get_root()));
    medicine->dealerDying = false;
    // Its first act is a 4.07 s wait, so dispatching it touches nothing yet.
    EXPECT_TRUE(medicine->get_node_owner()->call("UseMedicine_Dealer", {}));
}

// `dealer use handcuffs` cuffs the player through two METHOD keys:
// AttachHandCuffs at 0.4 s shows the player's cuffs 0.1 s later (after a
// RESET), and CheckPlayerHandCuffs_Animation at 0.6 s is the look down at them.
TEST(DealerItemMethods, AttachHandCuffsShowsThePlayersCuffs) {
    godot::SceneTree t;
    godot::Node* root = t.get_root();
    auto* cuffs = item_fixtures::Attach<scripts::HandcuffManager>(item_fixtures::Add<godot::Node>(t, "handcuff manager", root));
    cuffs->animator_handcuffsPlayer = item_fixtures::Add<godot::AnimationPlayer>(t, "animator_handcuffs player", root);
    cuffs->handcuffs_player = item_fixtures::Add<godot::Node3D>(t, "handcuffs player", root);
    cuffs->handcuffs_player->set_visible(false);

    EXPECT_TRUE(cuffs->get_node_owner()->call("AttachHandCuffs", {}));
    EXPECT_FALSE(cuffs->handcuffs_player->is_visible()) << "not before the 0.1 s wait";
    for (int i = 0; i < 12; ++i) t.process_frame(1.0 / 60.0);
    EXPECT_TRUE(cuffs->handcuffs_player->is_visible());
}
