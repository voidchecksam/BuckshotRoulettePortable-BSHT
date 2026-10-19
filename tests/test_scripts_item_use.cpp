// The player uses items: ItemInteraction and the small scripts each item
// reaches. On the item fixtures, with a real item taken from the briefcase and
// placed.
#include "item_fixtures.h"

#include "godot/material.h"
#include "godot/rng.h"
#include "scripts/DescriptionManager.h"
#include "scripts/BurnerPhone.h"
#include "godot/translation.h"
#include "scripts/InteractionManager.h"
#include "scripts/ItemInteraction.h"
#include "scripts/PermissionManager.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/FilterController.h"
#include "scripts/HandcuffManager.h"
#include "scripts/MedicineManager.h"
#include "scripts/SegmentManager.h"
#include "scripts/ShellExamine.h"
#include "scripts/CameraManager.h"
#include "scripts/ShellSpawner.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace item_fixtures;

namespace {

struct ItemUseTree : ItemTableTree {
    scripts::ItemInteraction* interaction = nullptr;
    scripts::PermissionManager* perm = nullptr;
    scripts::ShellSpawner* spawner = nullptr;
    scripts::ShellExamine* examine = nullptr;
    scripts::BurnerPhone* phone = nullptr;
    scripts::ShellEjectManager* ejector = nullptr;
    scripts::DealerIntelligence* dealerAi = nullptr;
    scripts::InteractionManager* interactionManager = nullptr;
    godot::Node3D* item = nullptr;   // the placed item, as the player sees it

    void SetUp() override {
        ItemTableTree::SetUp();
        if (!scenesLoaded) return;
        godot::Node* root = t.get_root()->get_child(0);
        godot::Node* managers = root->get_child(0);

        spawner = Attach<scripts::ShellSpawner>(Add<godot::Node>(t, "shell spawner", managers));
        roundManager->shellSpawner = spawner;
        spawner->sequenceArray = {"live", "blank", "live"};   // a loaded chamber
        roundManager->deskUI_parent = Add<godot::Control>(t, "desk ui", root);
        roundManager->deskUI_shotgun = Add<godot::Control>(t, "desk shotgun", root);
        roundManager->controller = items->controller;
        roundManager->cursor = items->cursor;

        auto* description = Attach<scripts::DescriptionManager>(Add<godot::Node>(t, "description", managers));
        for (int i = 0; i < 4; ++i) {
            description->uiArray.push_back(Add<godot::Label>(t, ("description text" + std::to_string(i)).c_str(), root));
        }
        interactionManager =
            Attach<scripts::InteractionManager>(Add<godot::Node>(t, "interaction manager", managers));

        interaction = Attach<scripts::ItemInteraction>(Add<godot::Node>(t, "item interaction", managers));
        perm = Attach<scripts::PermissionManager>(Add<godot::Node>(t, "permission manager", managers));
        perm->roundManager = roundManager;
        perm->itemManager = items;
        perm->description = description;
        perm->inter = interaction;
        perm->stackDisabledItemArray = {"handsaw", "magnifying glass", "beer", "cigarettes", "handcuffs", "adrenaline"};
        perm->stackDisabledItemArray_bools = std::vector<bool>(6, false);

        interaction->roundManager = roundManager;
        interaction->perm = perm;
        interaction->cursor = items->cursor;
        interaction->itemManager = items;
        interaction->items = items;
        interaction->amounts = amounts;
        interaction->camera = items->camera;
        interaction->lerpDuration = 0.4;
        interaction->pos_hand = {0.0f, 3.0f, 0.0f};
        interaction->speaker_pickup = Add<godot::AudioStreamPlayer>(t, "speaker_pickup", root);
        interaction->speaker_interaction = Add<godot::AudioStreamPlayer>(t, "speaker_interaction", root);
        interaction->animator_playerHands = Add<godot::AnimationPlayer>(t, "animator_player items", root);
        interaction->get_node_owner()->set_process(interaction->has_process());

        examine = Attach<scripts::ShellExamine>(Add<godot::Node>(t, "shell examine", managers));
        examine->shellSpawner = spawner;
        examine->shellParent = Add<godot::Node3D>(t, "shell parent", root);
        examine->mesh = Add<godot::MeshInstance3D>(t, "shell mesh", examine->shellParent);
        examine->mesh->add_surface_material(godot::SurfaceMaterial{});
        examine->mesh->add_surface_material(godot::SurfaceMaterial{});
        examine->mat_live = godot::MaterialLibrary::get("res://test/shell live.tres", godot::SurfaceMaterial{});
        examine->mat_blank = godot::MaterialLibrary::get("res://test/shell blank.tres", godot::SurfaceMaterial{});
        examine->mat_live->albedo = {1.0f, 0.0f, 0.0f, 1.0f};
        examine->mat_blank->albedo = {0.0f, 0.0f, 1.0f, 1.0f};

        dealerAi = Attach<scripts::DealerIntelligence>(Add<godot::Node>(t, "dealer intelligence", managers));
        dealerAi->sequenceArray_knownShell = {true, false, false};
        spawner->roundManager = roundManager;
        ejector = Attach<scripts::ShellEjectManager>(Add<godot::Node>(t, "shell eject manager", managers));
        ejector->shellSpawner = spawner;
        ejector->ai = dealerAi;
        ejector->mesh = Add<godot::MeshInstance3D>(t, "ejected shell", root);
        ejector->mesh->add_surface_material(godot::SurfaceMaterial{});
        ejector->mesh->add_surface_material(godot::SurfaceMaterial{});
        ejector->mesh->set_visible(false);
        ejector->animator = Add<godot::AnimationPlayer>(t, "animator_eject", root);
        ejector->animator_fader = Add<godot::AnimationPlayer>(t, "animator_fader", root);
        ejector->mat_live = godot::MaterialLibrary::get("res://test/eject live.tres", godot::SurfaceMaterial{});
        ejector->mat_blank = godot::MaterialLibrary::get("res://test/eject blank.tres", godot::SurfaceMaterial{});
        interaction->shellEject_player = ejector;

        phone = Attach<scripts::BurnerPhone>(Add<godot::Node>(t, "burner phone manager", managers));
        phone->sh = spawner;
        phone->dia = dialogue;

        TakeOne();
        item = items->activeItem;
        Place(0);
        // What main.tscn gives an item's indicator: its exports point into the
        // scene (absolute paths), which this small tree does not have.
        for (int i = 0; i < items->itemSpawnParent->get_child_count(); ++i) {
            godot::Node* placed = items->itemSpawnParent->get_child(i);
            if (auto* ind = godot::script_cast<scripts::PickupIndicator>(placed->get_child(0))) {
                ind->interaction = interactionManager;
                ind->description = description;
                ind->itemManager = items;
                ind->speaker_indicatorCheck = Add<godot::AudioStreamPlayer>(t, "speaker_indicator", root);
            }
        }
        RunFor(1.0);
    }

    scripts::InteractionBranch* Branch() { return godot::script_cast<scripts::InteractionBranch>(item->get_child(1)); }
};

}  // namespace

TEST_F(ItemUseTree, PickingAnItemLerpsItToTheHandThenFreesIt) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    t.spawn(interaction->PickupItemFromTable(item, Branch()->itemName));
    RunFor(1.0 / 60.0);
    EXPECT_TRUE(interaction->moving);
    RunFor(interaction->lerpDuration);
    EXPECT_TRUE(item->is_queued_for_deletion());
}

TEST_F(ItemUseTree, UsingAnItemCountsItOutOfThePlayersAmount) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    countFor("inverter")->amount_player = 1;
    t.spawn(interaction->InteractWith("inverter"));
    RunFor(1.0 / 60.0);
    EXPECT_EQ(countFor("inverter")->amount_player, 0);
    EXPECT_EQ(roundManager->playerCurrentTurnItemArray, std::vector<std::string>{"inverter"});
}

TEST_F(ItemUseTree, RemovingFromTheGridFreesItsSlot) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    ASSERT_TRUE(items->gridOccupiedArray[0]);
    const int before = items->numberOfOccupiedGrids;
    Branch()->itemGridIndex = 0;
    interaction->RemovePlayerItemFromGrid(item);
    EXPECT_FALSE(items->gridOccupiedArray[0]);
    EXPECT_EQ(items->numberOfOccupiedGrids, before - 1);
}

TEST_F(ItemUseTree, TheMagnifierShowsTheChamberShellsColour) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    examine->_ready();   // hidden until the animation asks for it
    ASSERT_FALSE(examine->shellParent->is_visible());
    spawner->sequenceArray = {"live", "blank"};
    examine->SetupShell();
    EXPECT_TRUE(examine->shellParent->is_visible());
    EXPECT_FLOAT_EQ(examine->mesh->get_active_surface_albedo(1).r, 1.0f);
    spawner->sequenceArray = {"blank"};
    examine->SetupShell();
    EXPECT_FLOAT_EQ(examine->mesh->get_active_surface_albedo(1).b, 1.0f);
    examine->RevertShell();
    EXPECT_FALSE(examine->shellParent->is_visible());
}

TEST_F(ItemUseTree, TheMagnifierAnimationReachesShellExamine) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live"};
    EXPECT_TRUE(examine->call("SetupShell", {}));
    EXPECT_TRUE(examine->call("RevertShell", {}));
    EXPECT_FALSE(examine->call("NoSuchMethod", {}));
}

TEST_F(ItemUseTree, TheInverterFlipsOnlyTheChamberShell) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live", "live", "blank"};
    t.spawn(interaction->InteractWith("inverter"));
    RunFor(1.0 / 60.0);
    EXPECT_EQ(spawner->sequenceArray, (std::vector<std::string>{"blank", "live", "blank"}));
}

TEST_F(ItemUseTree, ThePhoneNamesALaterShellNeverTheChamber) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    // gd: randi_range(1, len - 1): with two shells it can only be index 1.
    ASSERT_TRUE(godot::load_translations(Extracted("dialogue_en.json").c_str()));
    spawner->sequenceArray = {"live", "blank"};
    t.spawn(phone->SendDialogue());
    RunFor(1.0 / 60.0);
    // v2.2: the key carries "%s" and the phone fills it with nothing.
    const std::string expected = godot::tr("SEQUENCE2") + "\n... " + godot::format(godot::tr("BLANKROUND"), {""});
    EXPECT_EQ(dialogue->dialogueUI->get_text(), expected);
    EXPECT_EQ(expected.substr(expected.size() - 11), "...  BLANK.") << "no number in the text";
}

// v2.2: index 7 is read as 6 (as 8 already was), so the eighth shell is
// never named; SEQUENCE7 always describes shell index 6.
TEST_F(ItemUseTree, ThePhoneReadsIndexSevenAsSix) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    ASSERT_TRUE(godot::load_translations(Extracted("dialogue_en.json").c_str()));
    spawner->sequenceArray = {"blank", "blank", "blank", "blank", "blank", "blank", "live", "blank"};
    uint64_t seed = 1;
    for (;; ++seed) {
        godot::seed(seed);
        if (godot::randi_range(1, 7) == 7) break;
    }
    godot::seed(seed);
    t.spawn(phone->SendDialogue());
    RunFor(1.0 / 60.0);
    const std::string expected = godot::tr("SEQUENCE7") + "\n... " + godot::format(godot::tr("LIVEROUND"), {""});
    EXPECT_EQ(dialogue->dialogueUI->get_text(), expected);
}

TEST_F(ItemUseTree, OnTheLastShellThePhoneSaysUnfortunate) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live"};
    t.spawn(phone->SendDialogue());
    RunFor(1.0 / 60.0);
    EXPECT_EQ(dialogue->dialogueUI->get_text(), godot::tr("UNFORTUNATE"));
}

TEST_F(ItemUseTree, ThePhoneAnimationReachesTheBurnerPhoneManager) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live", "blank"};
    EXPECT_TRUE(phone->call("SendDialogue", {}));
}

TEST_F(ItemUseTree, SmokingCountsTheCigaretteAndTheStat) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    items->numberOfCigs_player = 2;
    t.spawn(interaction->InteractWith("cigarettes"));
    RunFor(1.0 / 60.0);
    EXPECT_EQ(playerData.stat_cigSmoked, 1);
    EXPECT_EQ(items->numberOfCigs_player, 2);   // only after the 5 s wait
    RunFor(5.1);
    EXPECT_EQ(items->numberOfCigs_player, 1);
}

TEST_F(ItemUseTree, BeerRacksTheChamberShellOutOfBothLists) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live", "blank", "live"};
    ejector->BeerEjection_player();
    EXPECT_EQ(spawner->sequenceArray, (std::vector<std::string>{"blank", "live"}));
    EXPECT_EQ(dealerAi->sequenceArray_knownShell, (std::vector<bool>{false, false}));
    EXPECT_EQ(playerData.stat_shellsEjected, 1);
    EXPECT_TRUE(ejector->mesh->is_visible());
}

TEST_F(ItemUseTree, TheBeerAnimationsReachTheEjectManager) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    spawner->sequenceArray = {"live", "blank", "live"};
    dealerAi->sequenceArray_knownShell = {false, false, false};
    EXPECT_TRUE(ejector->call("BeerEjection_player", {}));
    EXPECT_TRUE(ejector->call("BeerEjection_dealer", {}));
    EXPECT_EQ(spawner->sequenceArray.size(), 1u);
}

TEST_F(ItemUseTree, DrinkingCountsTheBeerStat) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    t.spawn(interaction->InteractWith("beer"));
    RunFor(1.0 / 60.0);
    EXPECT_EQ(playerData.stat_beerDrank, 330);
}

TEST_F(ItemUseTree, TheHandsawCutsTheBarrelToDoubleDamage) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    t.spawn(interaction->InteractWith("handsaw"));
    RunFor(1.0 / 60.0);
    EXPECT_TRUE(roundManager->barrelSawedOff);
    EXPECT_EQ(roundManager->currentShotgunDamage, 2);
}

struct SegmentTree : ItemUseTree {
    scripts::SegmentManager* segment = nullptr;
    godot::SurfaceMaterial mat;
    void SetUp() override {
        ItemUseTree::SetUp();
        if (!scenesLoaded) return;
        godot::Node* root = t.get_root()->get_child(0);
        segment = Attach<scripts::SegmentManager>(Add<godot::Node>(t, "segment manager", root->get_child(0)));
        mat.albedo = {1.0f, 1.0f, 1.0f, 1.0f};
        segment->mat = &mat;
        segment->roundManager = roundManager;
        segment->camera = items->camera;
        segment->speaker = Add<godot::AudioStreamPlayer>(t, "grow barrel", root);
        segment->get_node_owner()->set_process(segment->has_process());
    }
};

TEST_F(SegmentTree, HidingTheSegmentMakesItsMaterialClear) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    EXPECT_TRUE(segment->call("HideSegment", {}));
    EXPECT_FLOAT_EQ(mat.albedo.a, 0.0f);
}

TEST_F(SegmentTree, GrowingTheBarrelRestoresDamageAndFadesTheSegmentIn) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    segment->HideSegment();
    roundManager->barrelSawedOff = true;
    roundManager->currentShotgunDamage = 2;
    t.spawn(segment->GrowBarrel());
    RunFor(1.0 / 60.0);
    EXPECT_FALSE(roundManager->barrelSawedOff);
    EXPECT_EQ(roundManager->currentShotgunDamage, 1);
    RunFor(0.6 + 1.0);                       // ShowSegment at .6 s, then half the 2 s fade
    EXPECT_GT(mat.albedo.a, 0.3f);
    EXPECT_LT(mat.albedo.a, 0.7f);
    RunFor(1.5);
    EXPECT_FLOAT_EQ(mat.albedo.a, 1.0f);
}

// MedicineManager.GetFlip: `value < .5` is the HEAL (false = not dying) --
// v2.2 made it a coin flip (v1.2: .4). One randf_range draw.
TEST(MedicineFlip, HalfHealHalfHurt) {
    godot::seed(1);
    scripts::Medicine medicine;
    int dying = 0;
    for (int i = 0; i < 20000; ++i) dying += medicine.GetFlip() ? 1 : 0;
    EXPECT_NEAR(dying / 20000.0, 0.5, 0.015);
}

struct CuffTree : ItemUseTree {
    scripts::HandcuffManager* cuffs = nullptr;
    void SetUp() override {
        ItemUseTree::SetUp();
        if (!scenesLoaded) return;
        godot::Node* root = t.get_root()->get_child(0);
        dealerAi->roundManager = roundManager;
        dealerAi->camera = items->camera;
        dealerAi->animator_dealerHands = Add<godot::AnimationPlayer>(t, "animator_dealer hands", root);
        dealerAi->speaker_checkHandcuffs = Add<godot::AudioStreamPlayer>(t, "speaker_check", root);
        dealerAi->speaker_breakHandcuffs = Add<godot::AudioStreamPlayer>(t, "speaker_break", root);
        dealerAi->speaker_giveHandcuffs = Add<godot::AudioStreamPlayer>(t, "speaker_give", root);
        interaction->animator_dealerHands = dealerAi->animator_dealerHands;
        interaction->dealerIntelligence = dealerAi;
        cuffs = Attach<scripts::HandcuffManager>(Add<godot::Node>(t, "handcuff manager", root->get_child(0)));
        cuffs->dealerAI = dealerAi;
        cuffs->roundManager = roundManager;
        cuffs->camera = items->camera;
        cuffs->animator_handcuffsPlayer = Add<godot::AnimationPlayer>(t, "animator_handcuffs", root);
        cuffs->handcuffs_player = Add<godot::Node3D>(t, "handcuffs player", root);
    }
};

TEST_F(CuffTree, CuffingTheDealerSetsTheFlagsAfterTheCameraVisitsHim) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    dealerAi->dealerAboutToBreakFree = true;
    t.spawn(interaction->InteractWith("handcuffs"));
    RunFor(2.0);
    EXPECT_FALSE(roundManager->dealerCuffed);   // gd: set after 1 + 1.3 s
    RunFor(0.4);
    EXPECT_TRUE(roundManager->dealerCuffed);
    EXPECT_FALSE(dealerAi->dealerAboutToBreakFree);
}

TEST_F(CuffTree, TheCuffAnimationsReachTheDealer) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    EXPECT_TRUE(dealerAi->call("Animator_GiveHandcuffs", {}));
    EXPECT_TRUE(dealerAi->call("Animator_CheckHandcuffs", {}));
}

TEST_F(CuffTree, TheRoundEndBreaksTheDealersCuffs) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    roundManager->dealerCuffed = true;
    dealerAi->dealerAboutToBreakFree = true;
    t.spawn(cuffs->RemoveAllCuffsRoutine());
    RunFor(1.0 / 60.0);
    EXPECT_TRUE(roundManager->dealerCuffed);    // gd: after the .3 s camera move
    RunFor(0.4);
    EXPECT_FALSE(roundManager->dealerCuffed);
    EXPECT_FALSE(dealerAi->dealerAboutToBreakFree);
}

struct StealTree : ItemUseTree {
    void SetUp() override {
        ItemUseTree::SetUp();
        if (!scenesLoaded) return;
        godot::Node* root = t.get_root()->get_child(0);
        items->interaction = interaction;
        items->btnParent_stealing = Add<godot::Control>(t, "stealing ui", root);
        items->btnParent_stealing->set_visible(false);
        items->btnparent_ff_stealing = Add<godot::Control>(t, "stealing ff", root);
        interaction->pos_hand_main = interaction->pos_hand;
        interaction->pos_hand_stealing = {9.0f, 9.0f, 9.0f};
        interaction->anim_pp = Add<godot::AnimationPlayer>(t, "animator_pp", root);
        auto* filter = Attach<scripts::FilterController>(Add<godot::Node>(t, "filter", root->get_child(0)));
        interaction->filter = filter;
    }
};

TEST_F(StealTree, AdrenalineOpensTheDealersItemsForTheTaking) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    t.spawn(items->SetupItemSteal());
    RunFor(1.0 / 60.0);
    EXPECT_TRUE(interaction->stealing);
    EXPECT_FALSE(interaction->stealing_fs);           // gd: after the .7 s camera move
    EXPECT_FLOAT_EQ(interaction->pos_hand.x, 9.0f);   // the stealing hand pose
    RunFor(0.8);
    EXPECT_TRUE(interaction->stealing_fs);
    EXPECT_TRUE(items->btnParent_stealing->is_visible());
}

TEST_F(StealTree, SevenSecondsWithoutAChoiceRevertsTheSteal) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    t.spawn(items->SetupItemSteal());
    RunFor(0.8);
    ASSERT_TRUE(items->btnParent_stealing->is_visible());
    RunFor(7.1);
    EXPECT_FALSE(items->btnParent_stealing->is_visible());
    EXPECT_FALSE(interaction->stealing);
    RunFor(0.5);
    EXPECT_FALSE(interaction->stealing_fs);
    EXPECT_FLOAT_EQ(interaction->pos_hand.x, interaction->pos_hand_main.x);
}

TEST_F(StealTree, TheAdrenalineAnimationReachesItemInteraction) {
    if (!scenesLoaded) GTEST_SKIP() << "run the exporters (runtime_item_*.json)";
    EXPECT_TRUE(interaction->call("AdrenalineHit", {}));
}
