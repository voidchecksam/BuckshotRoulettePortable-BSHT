// Double or Nothing: the endless round, the score and the yes/no machine, the
// CRT and the local board, on the real main tree.
#include "main_tree_fixture.h"

#include "godot/material.h"
#include "godot/rng.h"
#include "game/achievements.h"
#include "scripts/AchievementManager.h"
#include "game/local_board.h"
#include "game/save_store.h"
#include "godot/input.h"
#include "godot/input_event.h"
#include "godot/translation.h"
#include "scripts/BriefcaseMachine.h"
#include "scripts/CrtButtonBranch.h"
#include "scripts/CrtIcon.h"
#include "scripts/CrtManager.h"
#include "scripts/DotRemover.h"
#include "scripts/LeaderboardManager.h"
#include "scripts/PartitionBranch.h"
#include "scripts/SaveFileManager.h"
#include "scripts/UserExit.h"
#include "scripts/DeathManager.h"
#include "scripts/CameraManager.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/EndingManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/EndlessMode.h"
#include "scripts/IntroManager.h"
#include "scripts/ItemAmounts.h"
#include "scripts/RoundBatch.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/intro_skip.h"
#include "scripts/resources.h"

#include "scripts/GlobalVariables.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using testing_support::MainTree;

template <class T>
T* Script(MainTree& m, const char* path) {
    return godot::script_cast<T>(m.tree.get_node(path));
}

constexpr const char* kEndless = "standalone managers/endless mode";
constexpr const char* kRound = "standalone managers/round manager";
constexpr const char* kIntro = "standalone managers/intro manager";
constexpr const char* kDealer = "standalone managers/dealer intelligence";
constexpr const char* kDisplay =
    "tabletop parent/main tabletop/health counter/health counter ui parent/round indicator parent";

struct DonTree : ::testing::Test {
    MainTree m;
    scripts::RoundManager* round = nullptr;

    // The CRT lives in the restroom intro: its tests keep the intro running.
    virtual bool SkipsIntro() const { return true; }

    void SetUp() override {
        if (!m.ok) GTEST_SKIP() << "exports missing";
        m.tree.ready_scripts();
        if (SkipsIntro()) scripts::SkipIntro(m.tree);
        round = Script<scripts::RoundManager>(m, kRound);
        ASSERT_NE(round, nullptr);
    }
};

// --- SetupEndless -------------------------------------------------------------

TEST_F(DonTree, EndlessBindsItsExports) {
    for (const std::string& e : m.bindErrors) ADD_FAILURE() << e;
    auto* endless = Script<scripts::Endless>(m, kEndless);
    ASSERT_NE(endless, nullptr);
    EXPECT_EQ(endless->roundManager, round);
    EXPECT_NE(endless->smokerdude, nullptr);
    ASSERT_NE(endless->display, nullptr) << "the shared .tres the rounds display draws with";
    EXPECT_EQ(endless->displayrounds, "inline_textures/misc_display_rounds_endless_png.png");
    EXPECT_NE(endless->amounts, nullptr);
}

// gd: EndlessMode.gd L9-L25
TEST_F(DonTree, SetupEndlessSkipsTheIntrosAndDealsTheDonPool) {
    auto* endless = Script<scripts::Endless>(m, kEndless);
    ASSERT_NE(endless, nullptr);
    auto* display = m.tree.get_node_as<godot::GeometryInstance3D>(kDisplay);
    ASSERT_NE(display, nullptr);
    EXPECT_EQ(display->get_active_surface_texture(0), "inline_textures/misc_display_rounds.png");

    endless->SetupEndless();

    EXPECT_FALSE(endless->smokerdude->is_visible());
    EXPECT_TRUE(round->endless);
    EXPECT_TRUE(round->shellLoadingSpedUp);
    EXPECT_TRUE(round->shellSpawner->skipDialoguePresented);
    const scripts::PlayerData& pd = *round->playerData;
    EXPECT_TRUE(pd.hasReadIntroduction);
    EXPECT_TRUE(pd.hasReadItemSwapIntroduction);
    EXPECT_TRUE(pd.hasReadItemDistributionIntro);
    EXPECT_TRUE(pd.hasReadItemDistributionIntro2);
    EXPECT_EQ(pd.numberOfDialogueRead, 3);
    EXPECT_TRUE(pd.skippingShellDescription);
    EXPECT_TRUE(pd.indicatorShown);
    EXPECT_TRUE(pd.cutterDialogueRead);
    EXPECT_TRUE(pd.seenGod);
    EXPECT_EQ(display->get_active_surface_texture(0), "inline_textures/misc_display_rounds_endless_png.png");
    ASSERT_FALSE(endless->amounts->array_amounts.empty());
    bool anyDonOnly = false;
    for (const scripts::AmountResource* res : endless->amounts->array_amounts) {
        EXPECT_EQ(res->amount_active, res->amount_don) << res->itemName;
        if (res->amount_main == 0 && res->amount_don > 0) anyDonOnly = true;
    }
    EXPECT_TRUE(anyDonOnly) << "Double or Nothing deals the four DoN-only items";
}

// gd: IntroManager.gd SelectedPill L230-L233: YES reaches SetupEndless after 2.05 s.
TEST_F(DonTree, ThePillsYesStartsEndless) {
    auto* intro = Script<scripts::IntroManager>(m, kIntro);
    ASSERT_NE(intro, nullptr);
    m.tree.spawn(intro->SelectedPill(true));
    m.Run(2.0);
    EXPECT_FALSE(round->endless);
    m.Run(0.1);
    EXPECT_TRUE(round->endless);
}

TEST_F(DonTree, ThePillsNoLeavesTheMainGame) {
    auto* intro = Script<scripts::IntroManager>(m, kIntro);
    ASSERT_NE(intro, nullptr);
    m.tree.spawn(intro->SelectedPill(false));
    m.Run(2.2);
    EXPECT_FALSE(round->endless);
}

// --- GenerateRandomBatches ----------------------------------------------------

// gd: RoundManager.gd L140-L154: four draws per round, in this order.
TEST_F(DonTree, RandomBatchesDrawHealthShellsItemsAndShuffle) {
    godot::seed(1234);
    struct Expect { int health, live, blank, items; bool shuffle; };
    std::vector<Expect> want;
    for (godot::Node* b : round->batchArray) {
        auto* batch = godot::script_cast<scripts::RoundBatch>(b);
        ASSERT_NE(batch, nullptr);
        for (std::size_t i = 0; i < batch->roundArray.size(); ++i) {
            Expect e{};
            e.health = static_cast<int>(godot::randi_range(2, 4));
            const int total = static_cast<int>(godot::randi_range(2, 8));
            e.live = std::max(1, total / 2);
            e.blank = total - e.live;
            e.items = static_cast<int>(godot::randi_range(2, 5));
            e.shuffle = godot::randi_range(0, 1) == 1;
            want.push_back(e);
        }
    }
    // Rounds the scene authors with shufflingArray already on stay on.
    std::vector<bool> shuffledBefore;
    for (godot::Node* b : round->batchArray) {
        for (const scripts::RoundClass* r : godot::script_cast<scripts::RoundBatch>(b)->roundArray) {
            shuffledBefore.push_back(r->shufflingArray);
        }
    }
    godot::seed(1234);
    round->GenerateRandomBatches();
    std::size_t k = 0;
    for (godot::Node* b : round->batchArray) {
        for (const scripts::RoundClass* r : godot::script_cast<scripts::RoundBatch>(b)->roundArray) {
            const Expect& e = want[k];
            EXPECT_EQ(r->startingHealth, e.health) << k;
            EXPECT_EQ(r->amountLive, e.live) << k;
            EXPECT_EQ(r->amountBlank, e.blank) << k;
            EXPECT_EQ(r->numberOfItemsToGrab, e.items) << k;
            EXPECT_TRUE(r->usingItems) << k;
            EXPECT_EQ(r->shufflingArray, e.shuffle || shuffledBefore[k]) << k;
            EXPECT_GE(r->startingHealth, 2);
            EXPECT_LE(r->startingHealth, 4);
            EXPECT_GE(r->amountLive + r->amountBlank, 2);
            EXPECT_LE(r->amountLive + r->amountBlank, 8);
            EXPECT_GE(r->amountLive, 1);
            ++k;
        }
    }
    EXPECT_EQ(k, want.size());
}

// gd: L157-L158: SetupRoundArray regenerates only in endless.
TEST_F(DonTree, SetupRoundArrayRegeneratesOnlyInEndless) {
    round->mainBatchIndex = 0;
    round->SetupRoundArray();
    ASSERT_FALSE(round->roundArray.empty());
    const int authored = round->roundArray[0]->startingHealth;
    const int authoredItems = round->roundArray[0]->numberOfItemsToGrab;
    round->SetupRoundArray();
    EXPECT_EQ(round->roundArray[0]->startingHealth, authored);
    round->endless = true;
    bool changed = false;
    for (int seed = 1; seed < 20 && !changed; ++seed) {
        godot::seed(static_cast<uint64_t>(seed));
        round->SetupRoundArray();
        changed = round->roundArray[0]->numberOfItemsToGrab != authoredItems ||
                  round->roundArray[0]->startingHealth != authored;
    }
    EXPECT_TRUE(changed);
    EXPECT_TRUE(round->roundArray[0]->usingItems) << "batch 1 has items in endless";
}

// --- the endless dealer -------------------------------------------------------

struct DealerTree : DonTree {
    scripts::DealerIntelligence* dealer = nullptr;
    void SetUp() override {
        DonTree::SetUp();
        if (IsSkipped()) return;
        dealer = Script<scripts::DealerIntelligence>(m, kDealer);
        ASSERT_NE(dealer, nullptr);
    }
    void Load(std::vector<std::string> seq, std::vector<bool> known) {
        dealer->shellSpawner->sequenceArray = std::move(seq);
        dealer->sequenceArray_knownShell = std::move(known);
    }
};

// gd: L282-L303
TEST_F(DealerTree, FigureOutShellKnowsAOneColourChamber) {
    Load({"live", "live", "live"}, {false, false, false});
    EXPECT_TRUE(dealer->FigureOutShell());
    Load({"blank", "blank"}, {false, false});
    EXPECT_TRUE(dealer->FigureOutShell());
}

TEST_F(DealerTree, FigureOutShellKnowsTheFrontShellFromMemory) {
    Load({"blank", "live"}, {true, false});
    EXPECT_TRUE(dealer->FigureOutShell());
}

TEST_F(DealerTree, FigureOutShellDeducesFromTheShellsItRemembers) {
    // Two live, one blank; it remembers both lives at the back: the front is blank.
    Load({"blank", "live", "live"}, {false, true, true});
    EXPECT_TRUE(dealer->FigureOutShell());
    // It remembers one live of two: it cannot tell.
    Load({"blank", "live", "live"}, {false, true, false});
    EXPECT_FALSE(dealer->FigureOutShell());
}

// gd: L421-L431: in endless the dealer bets on the majority.
TEST_F(DealerTree, EndlessCoinFlipBetsOnTheMajority) {
    round->endless = true;
    Load({"live", "live", "blank"}, {false, false, false});
    EXPECT_EQ(dealer->CoinFlip(), 1);
    Load({"live", "blank", "blank"}, {false, false, false});
    EXPECT_EQ(dealer->CoinFlip(), 0);
    Load({"live", "blank"}, {false, false});
    bool zero = false, one = false;
    for (int i = 0; i < 64; ++i) (dealer->CoinFlip() == 0 ? zero : one) = true;
    EXPECT_TRUE(zero && one) << "a tie is a true coin flip";
}

// Trap 6: outside endless the dealer stays a true 50/50 even with a majority.
TEST_F(DealerTree, MainGameCoinFlipIgnoresTheMajority) {
    Load({"live", "live", "live", "blank"}, {false, false, false, false});
    bool zero = false, one = false;
    for (int i = 0; i < 64; ++i) (dealer->CoinFlip() == 0 ? zero : one) = true;
    EXPECT_TRUE(zero && one);
}

// --- the score and the yes/no machine -----------------------------------------

struct ScoreTree : DonTree {
    void SetUp() override {
        DonTree::SetUp();
        if (IsSkipped()) return;
        round->endless = true;
        round->counting = false;
        round->initial_time = 120.0;   // two minutes: 70000 - 120/600 * 40000
    }
    scripts::InteractionBranch* Yes() { return round->intbranch_yes; }
    scripts::InteractionBranch* No() { return round->intbranch_no; }
};

// gd: L379-L420, the whole timeline.
TEST_F(ScoreTree, AClearedEndlessBatchLerpsTheScoreThenAsks) {
    ASSERT_NE(Yes(), nullptr);
    ASSERT_NE(No(), nullptr);
    m.tree.spawn(round->BeginScoreLerp());
    EXPECT_TRUE(round->doubling);
    EXPECT_EQ(round->endscore, 62000);
    EXPECT_EQ(round->double_or_nothing_rounds_beat, 1);
    EXPECT_EQ(round->double_or_nothing_initial_score, 62000);
    EXPECT_EQ(round->camera->activeSocket, "yes no");
    m.Run(1.0);
    EXPECT_FALSE(round->ui_score->is_visible());
    m.Run(0.15);
    EXPECT_TRUE(round->ui_score->is_visible());
    EXPECT_EQ(round->ui_score->get_text(), "0");
    m.Run(0.5 + 3.0);
    EXPECT_EQ(round->ui_score->get_text(), "62000");
    m.Run(0.55);   // 1.1 + .5 + 3.08 + .46 = 5.14
    EXPECT_FALSE(round->ui_score->is_visible());
    EXPECT_TRUE(round->ui_doubleornothing->is_visible());
    EXPECT_EQ(round->anim_doubleor->get_current_animation(), "show");
    EXPECT_FALSE(Yes()->interactionAllowed);
    m.Run(1.55);
    EXPECT_TRUE(Yes()->interactionAllowed);
    EXPECT_TRUE(No()->interactionAllowed);
    EXPECT_TRUE(round->btnParent_doubleor->is_visible());
}

TEST_F(ScoreTree, TheScoreHasAFloorOfTen) {
    round->initial_time = 60.0 * 60.0 * 5;
    m.tree.spawn(round->BeginScoreLerp());
    EXPECT_EQ(round->endscore, 10);
}

TEST_F(ScoreTree, DoubleOrNothingDoublesTheLastScore) {
    m.tree.spawn(round->BeginScoreLerp());
    m.Run(7.0);
    m.tree.spawn(round->BeginScoreLerp());
    EXPECT_TRUE(round->doubled);
    EXPECT_EQ(round->startscore, 62000);
    EXPECT_EQ(round->endscore, 124000);
    EXPECT_EQ(round->double_or_nothing_score, 124000);
    EXPECT_EQ(round->double_or_nothing_initial_score, 62000) << "the run's initial score stays";
    EXPECT_EQ(round->double_or_nothing_rounds_beat, 2);
}

// gd: L426-L451, L460-L474: YES restarts at batch 1.
TEST_F(ScoreTree, YesRestartsTheBatchAtTheStart) {
    m.tree.spawn(round->BeginScoreLerp());
    m.Run(7.0);
    round->playerData->currentBatchIndex = 3;
    round->mainBatchIndex = 2;
    m.tree.spawn(round->Response(true));
    // The buttons close at once, so a second press does nothing.
    EXPECT_FALSE(Yes()->interactionAllowed);
    EXPECT_FALSE(No()->interactionAllowed);
    EXPECT_FALSE(round->btnParent_doubleor->is_visible());
    EXPECT_FALSE(round->ui_doubleornothing->is_visible());
    EXPECT_EQ(round->anim_yes->get_current_animation(), "press");
    m.Run(0.45);
    EXPECT_EQ(round->anim_doubleor->get_current_animation(), "hide");
    m.Run(1.1);   // .4 + .4 + .7, then RestartBatch
    EXPECT_EQ(round->playerData->currentBatchIndex, 0);
    EXPECT_EQ(round->mainBatchIndex, 0);
    EXPECT_FALSE(round->brief->ending->endless_overwriting);
}

// A sawed-off barrel grows back before the batch is set up.
TEST_F(ScoreTree, RestartRegrowsASawedOffBarrelFirst) {
    round->barrelSawedOff = true;
    round->currentShotgunDamage = 2;
    round->mainBatchIndex = 2;
    round->playerData->currentBatchIndex = 3;
    m.tree.spawn(round->RestartBatch());
    EXPECT_EQ(round->playerData->currentBatchIndex, 0);
    m.Run(0.5);
    EXPECT_TRUE(round->barrelSawedOff);
    EXPECT_EQ(round->mainBatchIndex, 2) << "MainBatchSetup waits for the barrel";
    m.Run(0.2);
    EXPECT_FALSE(round->barrelSawedOff);
    EXPECT_EQ(round->currentShotgunDamage, 1);
    m.Run(3.2);   // GrowBarrel: .6 + 1.8 + .6
    EXPECT_EQ(round->mainBatchIndex, 0);
}

// gd: L436-L445: NO cashes out with the endless score.
TEST_F(ScoreTree, NoCashesOutThroughTheBriefcase) {
    m.tree.spawn(round->BeginScoreLerp());
    m.Run(7.0);
    m.tree.spawn(round->Response(false));
    EXPECT_EQ(round->anim_no->get_current_animation(), "press");
    m.Run(1.45);
    EXPECT_FALSE(round->brief->ending->endless_overwriting);
    m.Run(0.1);
    EXPECT_TRUE(round->brief->ending->endless_overwriting);
    EXPECT_EQ(round->brief->ending->endless_score, 62000);
    EXPECT_EQ(round->camera->activeSocket, "enemy");
}

// gd: L476-L493: batch 3 cleared in endless goes to the score, not the briefcase.
TEST_F(ScoreTree, ClearingTheLastEndlessBatchStartsTheScore) {
    round->counting = true;
    round->playerData->currentBatchIndex = 2;
    m.tree.spawn(round->EndMainBatch());
    m.Run(1.15);
    EXPECT_FALSE(round->doubling);
    m.Run(0.1);
    EXPECT_TRUE(round->doubling);
    EXPECT_FALSE(round->counting);
    EXPECT_EQ(round->camera->activeSocket, "yes no");
}

// A death in endless goes to death.tscn; ach10 only past 1,000,000.
struct EndlessDeath : ScoreTree {
    game::Achievements achievements;
    scripts::DeathManager* death = nullptr;
    void SetUp() override {
        ScoreTree::SetUp();
        if (IsSkipped()) return;
        ASSERT_TRUE(achievements.LoadText((std::string(BSHT_SOURCE_DIR) + "/tools/achievements_en.json").c_str()));
        Script<scripts::Achievement>(m, "standalone managers/achievement manager")->store = &achievements;
        death = Script<scripts::DeathManager>(m, "standalone managers/death manager");
        ASSERT_NE(death, nullptr);
    }
};

TEST_F(EndlessDeath, ADeathInEndlessGoesToTheDeathScreen) {
    round->endscore = 62000;
    m.tree.spawn(death->MainDeathRoutine());
    m.Run(0.6);
    EXPECT_EQ(m.tree.take_scene_change(), "res://scenes/death.tscn");
    EXPECT_FALSE(achievements.IsUnlocked("ach10"));
    EXPECT_FALSE(round->playerData->playerEnteringFromDeath) << "endless returns before the revival flags";
}

// The original reaches the next main only through SaveGame, which a death in
// endless skips (DeathManager.gd:141-146). What carries across a scene change
// is what SaveGame wrote this scene: nothing, here.
TEST_F(EndlessDeath, AnEndlessDeathSavesNothingToCarry) {
    auto* save = Script<scripts::SaveFileManager>(m, "standalone managers/save file manager");
    ASSERT_NE(save, nullptr);
    m.tree.spawn(death->MainDeathRoutine());
    m.Run(0.6);
    EXPECT_FALSE(save->savedThisScene.has_value());
}

TEST_F(EndlessDeath, AMainGameDeathCarriesWhatItSaved) {
    auto* save = Script<scripts::SaveFileManager>(m, "standalone managers/save file manager");
    ASSERT_NE(save, nullptr);
    round->endless = false;
    round->playerData->currentBatchIndex = 1;
    save->SaveGame();
    ASSERT_TRUE(save->savedThisScene.has_value());
    EXPECT_EQ(save->savedThisScene->currentBatchIndex, 1);
    round->playerData->currentBatchIndex = 2;   // after the save: not carried
    EXPECT_EQ(save->savedThisScene->currentBatchIndex, 1);
}

TEST_F(EndlessDeath, DyingPastAMillionUnlocksAch10) {
    round->endscore = 1000001;
    m.tree.spawn(death->MainDeathRoutine());
    EXPECT_TRUE(achievements.IsUnlocked("ach10"));
}

// --- the CRT ------------------------------------------------------------------

constexpr const char* kCrt = "standalone managers/crt manager";
constexpr const char* kBoard = "standalone managers/leaderboard manager";
constexpr const char* kCrtParent = "restroom_CLUB/bathroom wall main_crt hole/crt main parent";

std::string CrtPath(const std::string& rest) { return std::string(kCrtParent) + "/" + rest; }

std::string CrtTestDir(const char* name) {
    return std::string(BSHT_BINARY_DIR) + "/crt_test_" + name + "_" +
           ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

struct CrtTree : DonTree {
    scripts::CRT* crt = nullptr;
    scripts::Board* board = nullptr;
    bool SkipsIntro() const override { return false; }
    void SetUp() override {
        DonTree::SetUp();
        if (IsSkipped()) return;
        crt = Script<scripts::CRT>(m, kCrt);
        board = Script<scripts::Board>(m, kBoard);
        ASSERT_NE(crt, nullptr);
        ASSERT_NE(board, nullptr);
    }
    godot::Label3D* Name(int i) { return board->display_instances.at(static_cast<std::size_t>(i)); }
    godot::Label3D* Score(int i) { return godot::cast_to<godot::Label3D>(Name(i)->get_child(0)); }
};

TEST_F(CrtTree, TheCrtScriptsBindTheirExports) {
    for (const std::string& e : m.bindErrors) ADD_FAILURE() << e;
    EXPECT_EQ(crt->board, board);
    EXPECT_NE(crt->exit, nullptr);
    EXPECT_EQ(crt->iconbranches.size(), 4u);
    EXPECT_EQ(crt->array_partbranch.size(), 6u);
    EXPECT_EQ(board->crt, crt);
    EXPECT_EQ(board->display_instances.size(), 12u);
    auto* icon = crt->iconbranches.at(0);
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(icon->texture_active, "inline_textures/misc_crt_icon_top_leaderboard2.png");
    EXPECT_EQ(icon->texture_inactive, "inline_textures/misc_crt_icon_top_leaderboard_inactive.png");
}

// gd: CrtManager.gd L38-L40
TEST_F(CrtTree, TheStatsPageIsTheFirstShown) {
    EXPECT_TRUE(crt->screenparent_stats->is_visible());
    EXPECT_FALSE(crt->screenparent_leaderboard->is_visible());
}

// gd: L54-L73
TEST_F(CrtTree, SetCrtSwapsTheBathroomWallForTheHole) {
    crt->SetCRT(true);
    EXPECT_FALSE(crt->bathroom_normal->get_layer_mask_value(1));
    EXPECT_TRUE(crt->bathroom_broken->is_visible());
    EXPECT_TRUE(crt->mask->is_visible());
    for (godot::Node3D* o : crt->objarray_normal) EXPECT_FALSE(o->is_visible());
    for (godot::Node3D* o : crt->objarray_broken) EXPECT_TRUE(o->is_visible());
    crt->SetCRT(false);
    EXPECT_TRUE(crt->bathroom_normal->get_layer_mask_value(1));
    EXPECT_FALSE(crt->bathroom_broken->is_visible());
    EXPECT_FALSE(crt->mask->is_visible());
}

// v2.2 (gd: L54-L59): without Steam the wall stays whole; DisableCRT closes it.
TEST_F(CrtTree, TheCrtOpensOnlyWithSteamAndDisableCrtClosesIt) {
    const bool saved = scripts::GlobalVariables::using_steam;
    scripts::GlobalVariables::using_steam = false;
    crt->SetCRT(true);
    EXPECT_TRUE(crt->bathroom_normal->get_layer_mask_value(1));
    EXPECT_FALSE(crt->bathroom_broken->is_visible());
    scripts::GlobalVariables::using_steam = true;   // the port's value: the local board
    crt->SetCRT(true);
    EXPECT_TRUE(crt->bathroom_broken->is_visible());
    crt->DisableCRT();
    EXPECT_FALSE(crt->bathroom_broken->is_visible());
    EXPECT_TRUE(crt->bathroom_normal->get_layer_mask_value(1));
    scripts::GlobalVariables::using_steam = saved;
}

// v2.2 (gd: L77-L94): right and left on the top page fetch the next/previous 12.
TEST_F(CrtTree, RightAndLeftLoadTheNextAndPreviousTwelve) {
    game::SaveStore store(CrtTestDir("pages"));
    store.Clear(game::LocalBoard::kEntry);
    game::LocalBoard runs;
    for (int i = 0; i < 14; ++i) ASSERT_TRUE(runs.Append(&store, game::BoardRun{"R" + std::to_string(i), 100 + i, 1}));
    board->store = &store;
    m.Run(0.1);
    crt->CycleWindow();
    m.Run(0.1);
    ASSERT_EQ(Name(0)->get_text(), "#1 R13");
    m.tree.spawn(crt->Interaction("right"));
    m.Run(0.1);
    EXPECT_EQ(Name(0)->get_text(), "#13 R1");
    EXPECT_EQ(Name(1)->get_text(), "#14 R0");
    m.tree.spawn(crt->Interaction("left"));
    m.Run(0.1);
    EXPECT_EQ(Name(0)->get_text(), "#1 R13");
}

// CrtIcon.gd: inactive at _ready, active only for its own window.
TEST_F(CrtTree, AnIconLightsOnlyForItsWindow) {
    scripts::CrtIcon* icon = crt->iconbranches.at(1);
    auto* mesh = godot::cast_to<godot::GeometryInstance3D>(icon->get_node_owner()->get_parent());
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->get_active_surface_texture(0), icon->texture_inactive);
    icon->CheckState(1);
    EXPECT_EQ(mesh->get_active_surface_texture(0), icon->texture_active);
    icon->CheckState(2);
    EXPECT_EQ(mesh->get_active_surface_texture(0), icon->texture_inactive);
}

// CrtButtonBranch.gd: down over .06 s, up over .06 s; the branch is closed meanwhile.
TEST_F(CrtTree, AButtonDipsAndComesBackUp) {
    auto* button = Script<scripts::CrtButton>(m, CrtPath("crt button mesh_window/button branch_crt").c_str());
    ASSERT_NE(button, nullptr);
    ASSERT_NE(button->intbranch, nullptr);
    button->intbranch->interactionAllowed = true;
    m.tree.spawn(button->Press());
    EXPECT_FALSE(button->intbranch->interactionAllowed);
    m.Run(0.05);
    EXPECT_LT(button->parent->get_transform().Ty(), button->y_up);
    m.Run(0.15);
    EXPECT_TRUE(button->intbranch->interactionAllowed);
    EXPECT_NEAR(button->parent->get_transform().Ty(), button->y_up, 0.02);
}

// PartitionBranch.gd: the counters spin until every one has landed on 0.
TEST_F(CrtTree, PartitionsSpinDownToZero) {
    scripts::PartitionBranch* part = crt->array_partbranch.at(0);
    EXPECT_EQ(part->p->get_text(), "  '   '   '   '   '   '   '  ");
    godot::seed(7);
    part->Loop(true);
    m.Run(0.05);
    EXPECT_NE(part->p->get_text(), "  '   '   '   '   '   '   '  ");
    m.Run(60.0);
    EXPECT_TRUE(part->finished);
    EXPECT_FALSE(part->looping);
    EXPECT_EQ(part->p->get_text(), "0 ' 0 ' 0 ' 0 ' 0 ' 0 ' 0 ' 0");
    part->ResetPartition();
    EXPECT_EQ(part->p->get_text(), "  '   '   '   '   '   '   '  ");
    EXPECT_FALSE(part->finished);
}

// DotRemover.gd: a second after _ready the caption loses its dots and colons.
TEST_F(CrtTree, TheStatCaptionsLoseTheirDots) {
    auto* caption = m.tree.get_node_as<godot::Label3D>(
        CrtPath("crt screen main/crt screen_player stats/stats text6 total cash").c_str());
    auto* shells = m.tree.get_node_as<godot::Label3D>(
        CrtPath("crt screen main/crt screen_player stats/stats text1 shells ejected").c_str());
    ASSERT_NE(caption, nullptr);
    ASSERT_NE(shells, nullptr);
    m.Run(1.1);
    EXPECT_EQ(caption->get_text(), "TOTAL CASH ") << "the EN special case";
    const std::string fixed = shells->get_text();
    EXPECT_EQ(fixed.find('.'), std::string::npos);
    EXPECT_EQ(fixed.find(':'), std::string::npos);
    EXPECT_FALSE(fixed.empty());
}

// gd: L123-L166: the boot, then the stats page and the buttons.
TEST_F(CrtTree, BootupEndsOnTheStatsPageWithTheButtonsOpen) {
    m.Run(0.6);   // UserExit allows exiting 0.5 s after _ready
    m.tree.spawn(crt->Bootup());
    EXPECT_FALSE(crt->has_exited);
    EXPECT_EQ(crt->window_index, 3);
    m.Run(0.08);
    EXPECT_TRUE(crt->array_bootup.at(0)->is_visible());
    m.Run(12.0);
    EXPECT_TRUE(crt->viewing);
    EXPECT_FALSE(crt->exit->exitAllowed);
    for (godot::Node3D* l : crt->array_bootup) EXPECT_FALSE(l->is_visible());
    for (godot::Node3D* l : crt->array_bootuplogo) EXPECT_FALSE(l->is_visible());
    for (godot::Node3D* l : crt->array_stats) EXPECT_TRUE(l->is_visible());
    EXPECT_TRUE(crt->branch_window->interactionAllowed);
    EXPECT_TRUE(crt->branch_exit->interactionAllowed);
}

// gd: L101-L121: window cycles stats -> top -> overview -> friends -> stats.
TEST_F(CrtTree, TheWindowButtonCyclesThePages) {
    crt->CycleWindow();
    EXPECT_EQ(crt->window_index, 0);
    EXPECT_TRUE(crt->screenparent_leaderboard->is_visible());
    EXPECT_FALSE(crt->screenparent_stats->is_visible());
    EXPECT_TRUE(board->nocon->is_visible());
    EXPECT_EQ(board->active_alias, "top");
    crt->CycleWindow();
    EXPECT_EQ(board->active_alias, "overview");
    EXPECT_TRUE(board->lock->is_visible());
    crt->CycleWindow();
    EXPECT_EQ(board->active_alias, "friends");
    crt->CycleWindow();
    EXPECT_EQ(crt->window_index, 3);
    EXPECT_FALSE(crt->screenparent_leaderboard->is_visible());
    EXPECT_TRUE(crt->screenparent_stats->is_visible());
    EXPECT_FALSE(board->nocon->is_visible());
}

// An empty board shows nothing, unlocks nothing.
TEST_F(CrtTree, AnEmptyBoardShowsNoEntries) {
    game::SaveStore store(CrtTestDir("empty"));
    store.Clear(game::LocalBoard::kEntry);
    board->store = &store;
    game::Achievements achievements;
    ASSERT_TRUE(achievements.LoadText((std::string(BSHT_SOURCE_DIR) + "/tools/achievements_en.json").c_str()));
    board->ach->store = &achievements;
    m.Run(0.1);
    crt->CycleWindow();
    m.Run(0.1);
    for (int i = 0; i < 12; ++i) EXPECT_EQ(Name(i)->get_text(), "") << i;
    EXPECT_FALSE(achievements.IsUnlocked("ach16"));
    EXPECT_FALSE(board->checking) << "the local download answered";
}

// The local board's top page, as Steam's: "#rank name" and the doubled score.
TEST_F(CrtTree, TheTopPageListsTheLocalRuns) {
    game::SaveStore store(CrtTestDir("top"));
    store.Clear(game::LocalBoard::kEntry);
    game::LocalBoard runs;
    ASSERT_TRUE(runs.Append(&store, game::BoardRun{"ABE", 62000, 3}));
    ASSERT_TRUE(runs.Append(&store, game::BoardRun{"A_VERY_LONG_NAME_INDEED", 1000, 1}));
    board->store = &store;
    game::Achievements achievements;
    ASSERT_TRUE(achievements.LoadText((std::string(BSHT_SOURCE_DIR) + "/tools/achievements_en.json").c_str()));
    board->ach->store = &achievements;
    m.Run(0.1);
    crt->CycleWindow();
    m.Run(0.1);
    EXPECT_EQ(Name(0)->get_text(), "#1 ABE");
    EXPECT_EQ(Score(0)->get_text(), "248000");
    EXPECT_EQ(Name(1)->get_text(), "#2 A_VERY_LONG_NA-") << "18 characters, the last a dash";
    EXPECT_EQ(Score(1)->get_text(), "1000");
    EXPECT_EQ(Name(2)->get_text(), "");
    EXPECT_TRUE(achievements.IsUnlocked("ach16"));
    EXPECT_EQ(board->active_entry_count, 2);
}

// The overview: the ranks around the player's best run, "(rank/count)".
TEST_F(CrtTree, TheOverviewFindsThePlayersBestRun) {
    game::SaveStore store(CrtTestDir("overview"));
    store.Clear(game::LocalBoard::kEntry);
    game::LocalBoard runs;
    ASSERT_TRUE(runs.Append(&store, game::BoardRun{"TOP", 90000, 3}));
    ASSERT_TRUE(runs.Append(&store, game::BoardRun{"ABE", 62000, 2}));
    board->store = &store;
    round->playerData->playername = "ABE";
    m.Run(0.1);
    crt->CycleWindow();
    crt->CycleWindow();
    m.Run(0.1);
    EXPECT_TRUE(board->display_overview->is_visible());
    EXPECT_EQ(board->text_globalpos->get_text(), "(2/2)");
}

// v2.2 signs the waiver " ABE"; a run saved as "ABE" is still the player's.
TEST_F(CrtTree, TheOverviewFindsTheRunOfAV22WaiverName) {
    game::SaveStore store(CrtTestDir("overview_v22"));
    store.Clear(game::LocalBoard::kEntry);
    game::LocalBoard runs;
    ASSERT_TRUE(runs.Append(&store, game::BoardRun{"TOP", 90000, 3}));
    ASSERT_TRUE(runs.Append(&store, game::BoardRun{"ABE", 62000, 2}));
    board->store = &store;
    round->playerData->playername = " ABE";
    m.Run(0.1);
    crt->CycleWindow();
    crt->CycleWindow();
    m.Run(0.1);
    EXPECT_TRUE(board->display_overview->is_visible());
    EXPECT_EQ(board->text_globalpos->get_text(), "(2/2)");
}

// A page answered after the CRT was left is not drawn.
TEST_F(CrtTree, APageAnsweredAfterExitStaysBlank) {
    game::SaveStore store(CrtTestDir("exit"));
    store.Clear(game::LocalBoard::kEntry);
    game::LocalBoard runs;
    ASSERT_TRUE(runs.Append(&store, game::BoardRun{"ABE", 62000, 3}));
    board->store = &store;
    m.Run(0.1);
    crt->viewing = true;
    crt->CycleWindow();          // the download is queued
    m.tree.spawn(crt->Interaction("exit"));
    m.Run(0.1);
    EXPECT_TRUE(crt->has_exited);
    EXPECT_FALSE(crt->viewing);
    EXPECT_EQ(Name(0)->get_text(), "");
    m.Run(0.3);
    EXPECT_TRUE(crt->exit->exitAllowed);
}

// gd: L155-L165: the stats page reads the lifetime stats.
TEST_F(CrtTree, TheStatsPageReadsTheLifetimeStats) {
    game::SaveStore store(CrtTestDir("stats"));
    ASSERT_TRUE(store.Save(scripts::SaveFileManager::savePath_stats,
                           game::SaveMap{{"shots_fired", int64_t{14}}, {"shells_ejected", int64_t{18}},
                                         {"doors_kicked", int64_t{6}}, {"cigs_smoked", int64_t{5}},
                                         {"rounds_beat", int64_t{2}}, {"ml_drank", int64_t{1320}},
                                         {"total_cash", 131140.0}}));
    board->store = &store;
    board->UpdateStats();
    EXPECT_EQ(board->stat_shotsFired->get_text(), "14");
    EXPECT_EQ(board->stat_mlDrank->get_text(), "1320");
    EXPECT_EQ(board->stat_totalCash->get_text(), "131140");
}

// gd: L42-L52: the pad drives the CRT only while it is being viewed.
TEST_F(CrtTree, ThePadDrivesTheCrtOnlyWhileViewing) {
    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    input.set_dispatch([this](const godot::InputEventRef& e) { m.tree.push_input(e); });
    auto enter = [&](bool pressed) {
        auto k = std::make_shared<godot::InputEventKey>();
        k->keycode = godot::key::ENTER;   // ui_accept
        k->pressed = pressed;
        input.parse_input_event(k);
        input.flush_buffered_events();
    };
    enter(true);
    enter(false);
    EXPECT_EQ(crt->window_index, 3);
    crt->viewing = true;
    enter(true);
    enter(false);
    EXPECT_EQ(crt->window_index, 0);
    input.reset();
}

}  // namespace

// The Vita's hold-to-exit hint names its pad button.
TEST(ExitManagerVita, NamesThePadButton) {
    EXPECT_EQ(scripts::VitaButtonName(godot::joy::START), "START");
    EXPECT_EQ(scripts::VitaButtonName(godot::joy::A), "CROSS");
}

// GDScript ints are 64-bit; a long Double or Nothing run doubles the score past
// 2^31.
static_assert(sizeof(scripts::RoundManager{}.double_or_nothing_score) == 8, "scores are 64-bit as in GDScript");
static_assert(sizeof(scripts::EndingManager{}.endless_score) == 8, "scores are 64-bit as in GDScript");
