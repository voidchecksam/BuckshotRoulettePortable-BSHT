// ItemManager's dealing half, on a small tree wired the way main.tscn wires it.
//
// The briefcase models and the 23 camera sockets are not needed: the ceremony
// only calls into the managers, and those are the real translated scripts here.
// CameraManager.BeginLerp touches its camera only inside the socket loop, so a
// manager with no sockets moves nothing, the same trick the shooting tests use.
#include "extract/exporters.h"
#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/CameraManager.h"
#include "scripts/CompartmentManager.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/DialogueManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/ItemAmounts.h"
#include "scripts/ItemManager.h"
#include "scripts/gridBranch.h"
#include "scripts/PickupIndicator.h"
#include "scripts/item_scenes.h"
#include "godot/packed_scene.h"
#include "godot/physics.h"
#include "godot/runtime_scene_loader.h"

#include <cstdio>
#include "scripts/RoundManager.h"
#include "scripts/resources.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "item_fixtures.h"

using namespace item_fixtures;


// The whole first-time ceremony, both introductions included, ends with the
// player allowed to take from the briefcase, and the intros marked read.
TEST_F(ItemTree, TheCeremonyEndsWithTheIntakeOpen) {
    t.spawn(items->BeginItemGrabbing());
    RunFor(20.0);
    EXPECT_TRUE(intake->interactionAllowed);
    EXPECT_TRUE(playerData.hasReadItemSwapIntroduction);
    EXPECT_TRUE(playerData.hasReadItemDistributionIntro);
}

TEST_F(ItemTree, TheIntakeStaysClosedWhileTheCeremonyPlays) {
    t.spawn(items->BeginItemGrabbing());
    RunFor(1.0);
    EXPECT_FALSE(intake->interactionAllowed);
}

// Once both introductions have been read, the ceremony is only the compartment
// and the camera: 0.8 + 0.8 + 0.8 + 0.8 = 3.2 s to the intake. With the intros
// it is over 11 s, so the intake being open by 4 s proves both were skipped.
TEST_F(ItemTree, ALaterRoundSkipsTheIntroductions) {
    playerData.hasReadItemSwapIntroduction = true;
    playerData.hasReadItemDistributionIntro = true;
    t.spawn(items->BeginItemGrabbing());
    RunFor(4.0);
    EXPECT_TRUE(intake->interactionAllowed);
}

// The briefcase comes up and the items compartment opens.
TEST_F(ItemTree, TheCeremonyOpensTheItemsCompartment) {
    ASSERT_TRUE(comp->isHiding_items);
    t.spawn(items->BeginItemGrabbing());
    RunFor(20.0);
    EXPECT_FALSE(comp->isHiding_items);
}

// The item scenes' export files are named by extract::RuntimeExportName, and
// the bake and the runtime must find them by the same rule. A space in the
// scene name becomes an underscore in the FILE name only.
TEST(ItemScenes, TheExportFileNameMatchesWhatTheExtractorWrites) {
    EXPECT_EQ(scripts::ItemSceneExportFile("res://instances/item_handsaw.tscn"),
              "runtime_item_handsaw.json");
    EXPECT_EQ(scripts::ItemSceneExportFile("res://instances/item_beer can.tscn"),
              "runtime_item_beer_can.json");
    EXPECT_EQ(scripts::ItemSceneExportFile("res://instances/item_magnifying glass.tscn"),
              "runtime_item_magnifying_glass.json");
    for (const char* scene : {"res://instances/item_handsaw.tscn", "res://instances/item_beer can.tscn",
                              "res://instances/item_burner phone.tscn", "res://instances/item_expired medicine.tscn"}) {
        EXPECT_EQ(scripts::ItemSceneExportFile(scene), extract::RuntimeExportName(scene)) << scene;
    }
}

// Every item scene the game can deal must build. The bake FATALs on the first
// one that does not, but it only names the file; this names the error, for all
// nine at once. Reads the REAL exports, so it SKIPS rather than passes when
// the exporters have not been run.
TEST(ItemScenes, EveryRealItemExportBuilds) {
    const char* names[] = {"adrenaline", "beer_can", "burner_phone", "cigarettes",
                           "expired_medicine", "handcuffs", "handsaw", "inverter",
                           "magnifying_glass"};
    int found = 0;
    for (const char* n : names) {
        const std::string path =
            std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_item_" + n + ".json";
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        std::string json;
        char buf[65536];
        std::size_t k = 0;
        while ((k = std::fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, k);
        std::fclose(f);
        ++found;
        std::vector<godot::BuiltNode> built;
        std::string root;
        const godot::RuntimeSceneError e = godot::BuildRuntimeNodes(json, built, root);
        EXPECT_EQ(e, godot::RuntimeSceneError::None)
            << n << ": " << godot::RuntimeSceneErrorText(e);
    }
    if (found == 0) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
}

// The beer can is the only item with a cylinder collider. The shape's own
// maths is tested in test_godot_physics.cpp; this tests that the loader turns
// the exported numbers into the shape. A loader that falls back to
// CylinderShape3D's defaults (0.5, 2.0) fails here.
TEST(ItemScenes, TheBeerCansCylinderKeepsItsExportedSize) {
    const std::string path =
        std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_item_beer_can.json";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    std::string json;
    char buf[65536];
    std::size_t k = 0;
    while ((k = std::fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, k);
    std::fclose(f);

    std::vector<godot::BuiltNode> built;
    std::string root;
    ASSERT_EQ(godot::BuildRuntimeNodes(json, built, root), godot::RuntimeSceneError::None);
    const godot::CylinderShape3D* cylinder = nullptr;
    for (const godot::BuiltNode& b : built) {
        if (auto* cs = dynamic_cast<godot::CollisionShape3D*>(b.node.get())) {
            cylinder = dynamic_cast<const godot::CylinderShape3D*>(cs->shape.get());
            if (cylinder) break;
        }
    }
    ASSERT_NE(cylinder, nullptr) << "the beer can's collider is not a cylinder";
    EXPECT_NEAR(cylinder->radius, 0.78f, 1e-3f);   // item_beer can.tscn
    EXPECT_NEAR(cylinder->height, 4.9f, 1e-3f);
}

// ------------------------------------------------------------------ the draw
// GrabItem instantiates a real item scene, so these use two REAL exports (the
// handsaw and the inverter), registered under their real res:// paths, as the
// shell tests do. They skip when the exporters have not been run.

TEST_F(ItemDrawTree, AnItemAtItsCapIsNotOffered) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    countFor("handsaw")->amount_player = 1;   // at its cap of 1
    Grab();
    EXPECT_EQ(items->availableItemsToGrabArray_player, std::vector<std::string>{"inverter"});
}

TEST_F(ItemDrawTree, AnItemWithNoActiveAmountIsNeverOffered) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    countFor("handsaw")->amount_active = 0;
    Grab();
    EXPECT_EQ(items->availableItemsToGrabArray_player, std::vector<std::string>{"inverter"});
}

TEST_F(ItemDrawTree, AGrabCountsIntoTheDrawnItemsAmount) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    Grab();
    EXPECT_EQ(items->numberOfItemsGrabbed, 1);
    EXPECT_EQ(countFor("handsaw")->amount_player + countFor("inverter")->amount_player, 1);
    ASSERT_NE(items->activeItem, nullptr);
    ASSERT_NE(items->temp_interaction, nullptr);
    EXPECT_EQ(countFor(items->temp_interaction->itemName)->amount_player, 1);
}

// amount_main is a cap, not a weight. With a cap of 8 on one item and 1 on the
// other, the draw is still 50/50 while both are under their cap.
TEST_F(ItemDrawTree, TheDrawIsUniformNotWeightedByTheCap) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    countFor("handsaw")->amount_active = 8;
    int handsaws = 0;
    const int trials = 1000;
    for (int i = 0; i < trials; ++i) {
        countFor("handsaw")->amount_player = 0;
        countFor("inverter")->amount_player = 0;
        Grab();
        if (items->temp_interaction->itemName == "handsaw") ++handsaws;
    }
    EXPECT_NEAR(handsaws / static_cast<double>(trials), 0.5, 0.05);
}

TEST_F(ItemDrawTree, TheItemLerpsFromTheBriefcaseToTheHand) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    Grab();
    ASSERT_NE(items->activeItem, nullptr);
    const scripts::ItemResource* drawn = resourceFor(items->temp_interaction->itemName);
    const godot::Vector3 start = items->activeItem->get_position();
    EXPECT_NEAR(start.x, drawn->pos_inBriefcase.x, 0.5f);   // one frame in: still near the briefcase
    RunFor(items->lerpDuration + 0.2);
    const godot::Vector3 end = items->activeItem->get_position();
    EXPECT_NEAR(end.x, drawn->pos_inHand.x, 1e-3f);
    EXPECT_NEAR(end.y, drawn->pos_inHand.y, 1e-3f);
}

// The engine only runs a script's _process when has_process() says so
// (scene_tree.cpp:145). ItemManager's _process is what moves an item from the
// briefcase to the hand; without this override every item froze where it
// spawned, in the real game, while every other item test still passed.
TEST(ItemManagerProcess, ItemManagerAsksForAProcessEveryFrame) {
    scripts::ItemManager items;
    EXPECT_TRUE(items.has_process());
}

// RoundManager.gd:136-138. Outside endless, each item's cap for the round is
// its amount_main. amount_active is not authored data (all nine export 0), so
// until this runs every item is skipped and the first grab has nothing to draw
// from.
TEST(RoundManagerAmounts, TheBaseGameCapsEachItemAtItsMainAmount) {
    scripts::AmountResource saw;
    saw.itemName = "handsaw";
    saw.amount_main = 8;
    saw.amount_don = 3;
    scripts::AmountResource inverter;
    inverter.itemName = "inverter";
    inverter.amount_main = 0;   // not in the base game at all
    inverter.amount_don = 8;
    scripts::Amounts amounts;
    amounts.array_amounts = {&saw, &inverter};
    scripts::RoundManager round;
    round.amounts = &amounts;

    round.ParseMainGameAmounts();

    EXPECT_EQ(saw.amount_active, 8);
    EXPECT_EQ(inverter.amount_active, 0);
}

// ------------------------------------------- placing, the dealer, the clear
// A table wired the way main.tscn wires it: four player grid slots (child 0 an
// InteractionBranch, child 1 a StaticBody3D holding the slot's collider) and
// four dealer slots (child 0 a gridBranch naming the slot's activeIndex).

TEST_F(ItemTableTree, PlacingAnItemFillsThatSlot) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    TakeOne();
    Place(2);
    EXPECT_TRUE(items->gridOccupiedArray[2]);
    EXPECT_EQ(items->numberOfOccupiedGrids, 1);
    EXPECT_EQ(items->temp_interaction->itemGridIndex, 2);
    EXPECT_TRUE(items->temp_interaction->isPlayerSide);
    EXPECT_EQ(items->temp_indicator->dealerGridIndex, 2 + 8);   // gd: gridIndex + 8
}

// numberOfItemsToGrab is 2 in this fixture: the second placement ends the
// grab, and EndItemGrabbing draws the dealer's items. (Its final call,
// ReturnFromItemGrabbing, starts the shell routine, which this tree cannot
// show: it hosts no ShellSpawner.)
TEST_F(ItemTableTree, PlacingTheLastItemEndsTheGrabAndDealsTheDealer) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    TakeOne();
    Place(0);
    RunFor(items->lerpDuration + 0.1);
    TakeOne();
    Place(1);
    RunFor(1.0);   // inside EndItemGrabbing, before its hand-back at 2.35 s
    EXPECT_EQ(items->numberOfItemsGrabbed_enemy, 2);
    EXPECT_FALSE(intake->interactionAllowed);
}

TEST_F(ItemTableTree, TheDealerTakesAsManyAsThePlayerOntoFreeSlots) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    items->GrabItems_Enemy();
    EXPECT_EQ(items->numberOfItemsGrabbed_enemy, 2);
    EXPECT_EQ(items->itemArray_dealer.size(), 2u);
    EXPECT_EQ(items->gridParentArray_enemy_available.size(), 2u);   // two slots used
}

// GrabItems_Enemy draws twice per item (the item, then its slot), so a round
// of 2 consumes 4 numbers. One draw more or less and every later draw stops
// matching Godot's.
//
// Only when there is a choice: RandomPCG::random(from, to) returns without
// drawing when from == to (random_pcg.cpp:54, and rng.h:76 here). With a cap
// of 1 on each of two items the dealer's second pick has one candidate and
// draws nothing, so the caps are raised to keep both items on offer.
TEST_F(ItemTableTree, TheDealersDrawConsumesTwoNumbersPerItem) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    countFor("handsaw")->amount_active = 8;
    countFor("inverter")->amount_active = 8;
    godot::seed(99);
    items->GrabItems_Enemy();
    const int64_t next = godot::randi_range(0, 1 << 30);
    godot::seed(99);
    for (int i = 0; i < 2 * 2; ++i) (void)godot::randi_range(0, 1);
    EXPECT_EQ(godot::randi_range(0, 1 << 30), next);
}

TEST_F(ItemTableTree, ClearingTheTableResetsEveryCount) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    TakeOne();
    Place(0);
    items->GrabItems_Enemy();
    items->SetupItemClear();
    items->ClearAllItems();
    EXPECT_EQ(items->numberOfOccupiedGrids, 0);
    EXPECT_EQ(items->numberOfItemsGrabbed_enemy, 0);
    for (scripts::AmountResource* r : amounts->array_amounts) {
        EXPECT_EQ(r->amount_player, 0);
        EXPECT_EQ(r->amount_dealer, 0);
    }
}

// The "clear items" animation calls ClearAllItems through a METHOD track. A
// script without a call() dispatcher drops it silently, as EjectShell's would.
TEST_F(ItemTableTree, ClearAllItemsIsReachableFromItsAnimationTrack) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    EXPECT_TRUE(items->call("ClearAllItems", {}));
    EXPECT_FALSE(items->call("NotAMethod", {}));
}

namespace {

// v2.2: a first round that opens at 2 health deals no handsaw, to either side
// (ItemManager.GrabItem, GrabItems_Enemy).
TEST_F(ItemDrawTree, AFirstRoundAtTwoHealthOffersNoHandsaw) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    rounds[0].startingHealth = 2;
    Grab();
    EXPECT_EQ(items->availableItemsToGrabArray_player, std::vector<std::string>{"inverter"});
}

TEST_F(ItemDrawTree, AFirstRoundAtThreeHealthStillOffersIt) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    rounds[0].startingHealth = 3;
    Grab();
    EXPECT_EQ(items->availableItemsToGrabArray_player, (std::vector<std::string>{"handsaw", "inverter"}));
}

TEST_F(ItemTableTree, TheDealerGetsNoHandsawEitherAtTwoHealth) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json) first";
    rounds[0].startingHealth = 2;
    countFor("inverter")->amount_active = 2;   // enough left to deal two without the saw
    items->GrabItems_Enemy();
    for (const std::string& name : items->availableItemsToGrabArray_dealer) EXPECT_NE(name, "handsaw");
}

}  // namespace
