// The endings: the briefcase machine, the car, the unlock, and the
// hold-to-exit, on the real main tree.
#include "main_tree_fixture.h"

#include "godot/control.h"
#include "godot/input.h"
#include "godot/input_event.h"
#include "scripts/BriefcaseMachine.h"
#include "scripts/DialogueManager_Ending.h"
#include "scripts/EndingManager.h"
#include "scripts/GameOverManager.h"
#include "scripts/TextFire.h"
#include "scripts/UserExit.h"
#include "scripts/intro_skip.h"
#include "scripts/ControllerManager.h"
#include "scripts/InteractionManager.h"
#include "scripts/InteractionBranch.h"
#include <variant>
#include <cstdint>
#include "scripts/resources.h"
#include "scripts/SaveFileManager.h"
#include "scripts/RoundManager.h"
#include "scripts/DonUnlockManager.h"
#include "scripts/CameraManager.h"
#include "godot/translation.h"
#include "game/local_board.h"
#include "game/save_store.h"
#include "scripts/LeaderboardManager.h"

#include "print_capture.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

namespace {

using testing_support::MainTree;

template <class T>
T* Script(MainTree& m, const char* path) {
    return godot::script_cast<T>(m.tree.get_node(path));
}

constexpr const char* kEnding = "standalone managers/ending manager";
constexpr const char* kBrief = "standalone managers/briefcase machine";
constexpr const char* kExit = "Camera/dialogue UI/hold to exit label/hold to exit";

// --- bindings -----------------------------------------------------------------

TEST(EndingBindings, TheEndingScriptsBindTheirExports) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    for (const std::string& e : m.bindErrors) ADD_FAILURE() << e;
    auto* ending = Script<scripts::EndingManager>(m, kEnding);
    ASSERT_NE(ending, nullptr);
    EXPECT_EQ(ending->save, nullptr) << "main.tscn leaves EndingManager.save unset";
    EXPECT_NE(ending->pp, nullptr);
    EXPECT_NE(ending->dia, nullptr);
    EXPECT_NE(ending->exitm, nullptr);
    EXPECT_NE(ending->unlocker, nullptr);
    EXPECT_EQ(ending->fireArray.size(), 6u);
    EXPECT_EQ(ending->label_array.size(), 6u);
    EXPECT_EQ(ending->blockout.size(), 2u);
    auto* brief = Script<scripts::BriefcaseMachine>(m, kBrief);
    ASSERT_NE(brief, nullptr);
    EXPECT_EQ(brief->ending, ending);
    EXPECT_NE(brief->intbranch_lid, nullptr);
    EXPECT_NE(Script<scripts::ExitManager>(m, kExit), nullptr);
    EXPECT_NE(Script<scripts::GameOverManager>(m, "standalone managers/game over manager"), nullptr);
}

// --- ExitManager (UserExit.gd) ------------------------------------------------

struct ExitTree : ::testing::Test {
    MainTree m;
    scripts::ExitManager* exitm = nullptr;

    void SetUp() override {
        if (!m.ok) GTEST_SKIP() << "exports missing";
        m.tree.ready_scripts();
        scripts::SkipIntro(m.tree);
        exitm = Script<scripts::ExitManager>(m, kExit);
        ASSERT_NE(exitm, nullptr);
        godot::Input& input = godot::Input::get_singleton();
        input.reset();
        input.set_dispatch([this](const godot::InputEventRef& e) { m.tree.push_input(e); });
    }
    void TearDown() override { godot::Input::get_singleton().reset(); }

    void Escape(bool pressed) {
        auto k = std::make_shared<godot::InputEventKey>();
        k->keycode = godot::key::ESCAPE;
        k->pressed = pressed;
        godot::Input::get_singleton().parse_input_event(k);
        godot::Input::get_singleton().flush_buffered_events();
    }
};

// gd: L10-L12, L14-L22, L41-L47: allowed 0.5 s after _ready; held over 2 s -> the menu.
TEST_F(ExitTree, HoldingExitForTwoSecondsReturnsToTheMenu) {
    testing_support::PrintCapture log;
    m.Run(0.6);
    ASSERT_TRUE(exitm->exitAllowed);
    Escape(true);
    m.Run(2.1);
    EXPECT_EQ(m.tree.take_scene_change(), "res://scenes/menu.tscn");
    EXPECT_TRUE(log.Has("changing scene to: menu"));   // v2.2
    EXPECT_FALSE(exitm->exitAllowed) << "CheckExit asks once";
}

TEST_F(ExitTree, ReleasingBeforeTwoSecondsResetsTheHold) {
    m.Run(0.6);
    Escape(true);
    m.Run(1.8);
    Escape(false);
    m.Run(0.1);
    Escape(true);
    m.Run(1.8);
    EXPECT_EQ(m.tree.take_scene_change(), "");
    EXPECT_LT(exitm->total, 2.0);
}

// gd: L24-L39: the hint names the bind for the input the player is using.
TEST_F(ExitTree, ThePressShowsTheHoldHint) {
    m.Run(0.6);
    Escape(true);
    m.Run(1.0 / 60.0);
    ASSERT_NE(exitm->label, nullptr);
    EXPECT_EQ(exitm->label->get_text(), "HOLD 'Escape' TO EXIT ...");
    EXPECT_EQ(exitm->anim->get_current_animation(), "fade out");
}

// BeginEnding sets exitAllowed = false; the hold no longer exits.
TEST_F(ExitTree, NotAllowedMeansNoExit) {
    m.Run(0.6);
    exitm->exitAllowed = false;
    Escape(true);
    m.Run(2.5);
    EXPECT_EQ(m.tree.take_scene_change(), "");
}

// --- DialogueEnding (DialogueManager_Ending.gd) -------------------------------

struct EndingTree : ::testing::Test {
    MainTree m;
    void SetUp() override {
        if (!m.ok) GTEST_SKIP() << "exports missing";
        m.tree.ready_scripts();
        scripts::SkipIntro(m.tree);
    }
    scripts::EndingManager* Ending() { return Script<scripts::EndingManager>(m, kEnding); }
    scripts::DialogueEnding* Dia() { return Ending()->dia; }
};

// gd: L13-L14: the click stream is the first of the array.
TEST_F(EndingTree, DialogueEndingLoadsTheFirstClick) {
    ASSERT_NE(Dia(), nullptr);
    EXPECT_EQ(Dia()->speaker_click->get_stream(), Dia()->soundArray_clicks.at(0));
}

// gd: L16-L22, L30-L40: one character now, one more every incrementDelay (0.08 s),
// a click each at pitch 0.8-1; the loop stops once every character shows.
TEST_F(EndingTree, ShowTextForeverTypesOneCharacterPerDelay) {
    scripts::DialogueEnding* dia = Dia();
    ASSERT_NE(dia, nullptr);
    dia->ShowText_Forever("ABC");
    EXPECT_TRUE(dia->dialogueUI->is_visible());
    EXPECT_EQ(dia->dialogueUI->get_text(), "ABC");
    EXPECT_EQ(dia->dialogueUI->get_visible_characters(), 1);
    EXPECT_TRUE(dia->looping);
    const float pitch = dia->speaker_click->get_pitch_scale();
    EXPECT_GE(pitch, 0.8f);
    EXPECT_LE(pitch, 1.0f);
    m.Run(0.09);
    EXPECT_EQ(dia->dialogueUI->get_visible_characters(), 2);
    m.Run(0.08);
    EXPECT_FALSE(dia->looping) << "the ratio reached 1 on the third character";
    EXPECT_GE(dia->dialogueUI->get_visible_ratio(), 1.0f);
}

TEST_F(EndingTree, OverridingLowersTheClickPitch) {
    scripts::DialogueEnding* dia = Dia();
    ASSERT_NE(dia, nullptr);
    dia->overriding = true;
    dia->ShowText_Forever("ABCD");
    const float pitch = dia->speaker_click->get_pitch_scale();
    EXPECT_GE(pitch, 0.2f);
    EXPECT_LE(pitch, 0.4f);
}

TEST_F(EndingTree, HideTextStopsAndHides) {
    scripts::DialogueEnding* dia = Dia();
    ASSERT_NE(dia, nullptr);
    dia->ShowText_Forever("ABCDEFGH");
    dia->HideText();
    EXPECT_FALSE(dia->looping);
    EXPECT_FALSE(dia->dialogueUI->is_visible());
    m.Run(0.3);
    EXPECT_EQ(dia->dialogueUI->get_visible_characters(), 1) << "the loop ended at its next wake";
}

// --- TextFire -----------------------------------------------------------------

// gd: L16-L18: ui is the parent, speaker the parent's child 1.
TEST_F(EndingTree, TextFireTakesItsParentAndSpeaker) {
    scripts::TextFire* fire = Ending()->fireArray.at(0);
    ASSERT_NE(fire, nullptr);
    EXPECT_EQ(fire->ui, fire->get_node_owner()->get_parent());
    ASSERT_NE(fire->speaker, nullptr);
    EXPECT_EQ(static_cast<godot::Node*>(fire->speaker), fire->ui->get_child(1));
}

// gd: L20-L40: shown at (max_x, max_y), eased back to the scale it had.
TEST_F(EndingTree, FireScalesFromMaxBackToTheOriginal) {
    scripts::TextFire* fire = Ending()->fireArray.at(0);
    ASSERT_NE(fire, nullptr);
    const godot::Vector2 orig = fire->ui->get_scale();
    fire->Fire();
    EXPECT_TRUE(fire->ui->is_visible());
    EXPECT_TRUE(fire->moving);
    m.Run(1.0 / 60.0);
    // One frame of ease(x, 0.4) = 1 - (1 - x)^(1 / 0.4) (math_funcs.cpp:95),
    // x = 1/60 / duration.
    const double c = 1.0 - std::pow(1.0 - (1.0 / 60.0) / fire->duration, 1.0 / 0.4);
    EXPECT_NEAR(fire->ui->get_scale().x, fire->max_x + (orig.x - fire->max_x) * c, 1e-3);
    m.Run(fire->duration + 0.1);
    EXPECT_NEAR(fire->ui->get_scale().x, orig.x, 1e-5);
    EXPECT_NEAR(fire->ui->get_scale().y, orig.y, 1e-5);
    const float pitch = fire->speaker->get_pitch_scale();
    EXPECT_GE(pitch, 0.95f);
    EXPECT_LE(pitch, 1.0f);
}

TEST_F(EndingTree, AChangedFireKeepsItsPitch) {
    scripts::TextFire* fire = Ending()->fireArray.at(5);
    ASSERT_NE(fire, nullptr);
    fire->changed = true;
    fire->speaker->set_pitch_scale(0.15f);
    fire->Fire();
    EXPECT_FLOAT_EQ(fire->speaker->get_pitch_scale(), 0.15f);
}

// --- EndingManager ------------------------------------------------------------

std::string EndingTestDir(const char* name) {
    return std::string(BSHT_BINARY_DIR) + "/ending_test_" + name + "_" +
           ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

void SetStats(scripts::RoundManager* rm, int shots, int shells, int doors, int cigs, int beer) {
    rm->playerData->playername = "ABE";
    rm->playerData->stat_shotsFired = shots;
    rm->playerData->stat_shellsEjected = shells;
    rm->playerData->stat_doorsKicked = doors;
    rm->playerData->stat_cigSmoked = cigs;
    rm->playerData->stat_beerDrank = beer;
}

// v2.2 (gd: L136-L176): each stat line is its key, a space and 64 dots; the
// number goes to the line's child 2 (`stat`). The cash line keeps its number.
const std::string kDots(64, '.');

godot::Label* StatLabel(scripts::EndingManager* e, int line) {
    return godot::cast_to<godot::Label>(e->label_array[static_cast<std::size_t>(line)]->get_child(2));
}

TEST_F(EndingTree, FinalScoreMatchesGodotsLabels) {
    scripts::EndingManager* e = Ending();
    SetStats(e->roundManager, 7, 9, 3, 2, 660);
    e->FinalScore();
    EXPECT_DOUBLE_EQ(e->cash_gl, 65570.0);
    EXPECT_EQ(e->glob_text_congratulations, "CONGRATULATIONS, ABE!");
    const char* keys[] = {"SHOTS FIRED", "SHELLS EJECTED", "DOORS KICKED", "CIGS SMOKED", "ML DRANK"};
    const char* stats[] = {"7", "9", "3", "2", "660"};
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(e->label_array[static_cast<std::size_t>(i)]->get_text(), godot::tr(keys[i]) + " " + kDots) << i;
        ASSERT_NE(StatLabel(e, i), nullptr) << i;
        EXPECT_EQ(StatLabel(e, i)->get_text(), stats[i]) << i;
    }
    EXPECT_EQ(e->label_array[5]->get_text(), "TOTAL CASH: 65570 $");
}

TEST_F(EndingTree, FinalScoreChargesLegBonesOnlyAboveTwoDoorsAndNeverGoesNegative) {
    scripts::EndingManager* e = Ending();
    SetStats(e->roundManager, 0, 0, 2, 0, 1);
    e->FinalScore();
    EXPECT_DOUBLE_EQ(e->cash_gl, 70000.0 - 1.5);
    EXPECT_EQ(e->label_array[5]->get_text(), "TOTAL CASH: 69998.5 $");
    SetStats(e->roundManager, 0, 0, 0, 0, 100000);
    e->FinalScore();
    EXPECT_DOUBLE_EQ(e->cash_gl, 0.0);
    EXPECT_EQ(e->label_array[5]->get_text(), "TOTAL CASH: 0 $");
}

TEST_F(EndingTree, EndlessOverwritesTheCashAndTheFirstLine) {
    scripts::EndingManager* e = Ending();
    SetStats(e->roundManager, 7, 9, 3, 2, 660);
    e->endless_overwriting = true;
    e->endless_score = 140000;
    e->endless_roundsbeat = 4;
    e->FinalScore();
    EXPECT_DOUBLE_EQ(e->cash_gl, 140000.0);
    EXPECT_EQ(e->label_array[0]->get_text(), "ROUNDS BEAT " + kDots);
    ASSERT_NE(StatLabel(e, 0), nullptr);
    EXPECT_EQ(StatLabel(e, 0)->get_text(), "4");
}

// gd: L192-L230: lifetime stats add up in the store, including the original's
// cigs_smoked, saved as the previous doors_kicked plus this run's cigarettes
// (gd:197).
TEST_F(EndingTree, FinalScoreAddsTheRunToTheLifetimeStats) {
    game::SaveStore store(EndingTestDir("stats"));
    store.Clear(scripts::SaveFileManager::savePath_stats);
    scripts::EndingManager* e = Ending();
    e->store = &store;
    SetStats(e->roundManager, 7, 9, 3, 2, 660);
    e->FinalScore();
    e->FinalScore();
    game::SaveMap data;
    ASSERT_TRUE(store.Load(scripts::SaveFileManager::savePath_stats, data));
    EXPECT_EQ(std::get<std::int64_t>(data.at("shots_fired")), 14);
    EXPECT_EQ(std::get<std::int64_t>(data.at("doors_kicked")), 6);
    EXPECT_EQ(std::get<std::int64_t>(data.at("cigs_smoked")), 3 + 2) << "previous DOORS + cigs (gd:197)";
    EXPECT_EQ(std::get<std::int64_t>(data.at("ml_drank")), 1320);
    EXPECT_DOUBLE_EQ(std::get<double>(data.at("total_cash")), 131140.0);
}

// gd: L173: an endless cash-out uploads the run, here to the local board: the
// waiver name, the initial score and the rounds beaten, on SaveStore "board".
TEST_F(EndingTree, AnEndlessCashOutGoesOnTheLocalBoard) {
    game::SaveStore store(EndingTestDir("board"));
    store.Clear(game::LocalBoard::kEntry);
    scripts::EndingManager* e = Ending();
    ASSERT_NE(e->board, nullptr);
    e->board->store = &store;
    SetStats(e->roundManager, 7, 9, 3, 2, 660);
    e->roundManager->double_or_nothing_rounds_beat = 3;
    e->roundManager->double_or_nothing_score = 248000;
    e->roundManager->double_or_nothing_initial_score = 62000;
    e->FinalScore();
    game::LocalBoard board;
    board.Load(&store);
    EXPECT_TRUE(board.Runs().empty()) << "a main-game win uploads nothing";
    e->endless_overwriting = true;
    e->FinalScore();
    board.Load(&store);
    ASSERT_EQ(board.Runs().size(), 1u);
    EXPECT_EQ(board.Runs()[0].name, "ABE");
    EXPECT_EQ(board.Runs()[0].initial, 62000);
    EXPECT_EQ(board.Runs()[0].beaten, 3);
}

TEST_F(EndingTree, NoStoreMeansNoLifetimeStats) {
    scripts::EndingManager* e = Ending();
    SetStats(e->roundManager, 7, 9, 3, 2, 660);
    e->FinalScore();   // must not crash; a diagnostic boot has no store
    EXPECT_DOUBLE_EQ(e->cash_gl, 65570.0);
}

// gd: L45-L87, L116-L127: the timeline of BeginEnding.
TEST_F(EndingTree, BeginEndingSnapsToTheCarAndGradesIt) {
    scripts::EndingManager* e = Ending();
    SetStats(e->roundManager, 7, 9, 3, 2, 660);
    e->exitm->exitAllowed = true;
    e->BeginEnding();
    EXPECT_FALSE(e->exitm->exitAllowed);
    EXPECT_EQ(e->animator_pan->get_current_animation(), "pan to brief");
    m.Run(10.6);
    EXPECT_FALSE(e->viewblocker->is_visible());
    m.Run(0.1);
    EXPECT_TRUE(e->viewblocker->is_visible());
    EXPECT_EQ(e->animator_viewblocker->get_current_animation(), "snap");
    m.Run(0.2);
    EXPECT_TRUE(e->uiparent->is_visible());
    for (godot::Node3D* b : e->blockout) EXPECT_TRUE(b->is_visible());
    EXPECT_FLOAT_EQ(e->pp->get_adjustment_brightness(), 5.68f);
    EXPECT_FLOAT_EQ(e->pp->get_adjustment_contrast(), 1.3f);
    EXPECT_FLOAT_EQ(e->pp->get_adjustment_saturation(), .26f);
    EXPECT_FLOAT_EQ(e->cam->cam->get_fov(), 60.0f);
    EXPECT_EQ(e->animator_cam->get_current_animation(), "camera loop car");
    EXPECT_FALSE(e->label_congrats->is_visible());
    for (godot::Label* l : e->label_array) EXPECT_FALSE(l->is_visible());
    EXPECT_FALSE(e->waitingForInput);
}

TEST_F(EndingTree, BeginEndingShowsTheStatsThenWaitsForAKey) {
    scripts::EndingManager* e = Ending();
    SetStats(e->roundManager, 7, 9, 3, 2, 660);
    e->BeginEnding();
    // 10.67 + 1.2 + 3 + 3 = 17.87: the congratulations line (each timer resumes
    // the frame after it expires, so a few frames of margin).
    m.Run(18.0);
    EXPECT_TRUE(e->label_congrats->is_visible());
    EXPECT_EQ(e->animator_congrats->get_current_animation(), "wobble it");
    EXPECT_TRUE(e->dia->looping);
    // + 2 + 6 x .15 + .5 + (6 x .33 + .4) + 3 = 8.78 -> "flicker" at ~26.65,
    // then GetInput's .8 -> waiting at ~27.45.
    m.Run(8.9);
    for (godot::Label* l : e->label_array) EXPECT_TRUE(l->is_visible());
    EXPECT_FALSE(e->waitingForInput);
    EXPECT_EQ(e->animator_anykey->get_current_animation(), "flicker");
    m.Run(0.8);
    EXPECT_TRUE(e->waitingForInput);
    EXPECT_FLOAT_EQ(e->fireArray[5]->speaker->get_pitch_scale(), .15f);
}

void PressKey(godot::SceneTree& t) {
    auto k = std::make_shared<godot::InputEventKey>();
    k->keycode = 65;   // any key
    k->pressed = true;
    t.push_input(k);
}

// gd: L39-L43, L94-L106, DonUnlockManager.gd L10-L28.
TEST_F(EndingTree, AKeyBeforeTheEndIsIgnoredAndTheFirstWinUnlocksDoubleOrNothing) {
    testing_support::PrintCapture log;
    game::SaveStore store(EndingTestDir("unlock"));
    store.Clear(scripts::Unlocker::savepath);
    scripts::EndingManager* e = Ending();
    e->unlocker->store = &store;
    PressKey(m.tree);
    EXPECT_EQ(e->animator_viewblocker->get_current_animation(), "") << "not waiting: ignored";
    e->waitingForInput = true;
    PressKey(m.tree);
    EXPECT_FALSE(e->waitingForInput);
    EXPECT_EQ(e->animator_viewblocker->get_current_animation(), "fade in");
    PressKey(m.tree);   // a second key starts nothing
    m.Run(4.05);
    EXPECT_TRUE(store.Exists(scripts::Unlocker::savepath)) << "UnlockMode wrote pills";
    m.Run(1.0);
    EXPECT_TRUE(e->unlocker->ui->is_visible());
    EXPECT_TRUE(e->dia->overriding);
    EXPECT_EQ(e->dia->dialogueUI, e->unlocker->ui);
    EXPECT_EQ(e->unlocker->ui->get_text(), godot::tr("DOUBLE UNLOCK"));
    m.Run(4.0);
    EXPECT_EQ(e->unlocker->anim->get_current_animation(), "fade");
    EXPECT_EQ(m.tree.take_scene_change(), "");
    m.Run(1.05);
    EXPECT_EQ(m.tree.take_scene_change(), "res://scenes/menu.tscn");
    EXPECT_TRUE(log.Has("changing scene to: menu")) << "DonUnlockManager, v2.2";
}

TEST_F(EndingTree, AlreadyUnlockedGoesStraightToTheMenu) {
    testing_support::PrintCapture log;
    game::SaveStore store(EndingTestDir("unlocked"));
    scripts::EndingManager* e = Ending();
    e->unlocker->store = &store;
    e->unlocker->UnlockMode();
    e->waitingForInput = true;
    PressKey(m.tree);
    m.Run(3.9);
    EXPECT_EQ(m.tree.take_scene_change(), "");
    m.Run(0.2);
    EXPECT_EQ(m.tree.take_scene_change(), "res://scenes/menu.tscn");
    EXPECT_TRUE(log.Has("changing scene to: menu")) << "EndingManager, v2.2";
}

TEST_F(EndingTree, WithNoStoreTheUnlockWritesNothingAndStillReachesTheMenu) {
    scripts::EndingManager* e = Ending();
    e->unlocker->store = nullptr;
    e->waitingForInput = true;
    PressKey(m.tree);
    m.Run(4.05 + 1.0 + 4.0 + 1.05);
    EXPECT_EQ(m.tree.take_scene_change(), "res://scenes/menu.tscn");
}

// v2.2 (gd: L39-L43): _unhandled_input takes any pressed event, so a pad
// button ends the ending as a key does; a mouse motion does not.
TEST_F(EndingTree, APadButtonEndsTheEndingAsAKeyDoes) {
    scripts::EndingManager* e = Ending();
    e->waitingForInput = true;
    auto motion = std::make_shared<godot::InputEventMouseMotion>();
    m.tree.push_input(motion);
    EXPECT_TRUE(e->waitingForInput) << "a motion is not a press";
    auto pad = std::make_shared<godot::InputEventJoypadButton>();
    pad->button_index = godot::joy::A;
    pad->pressed = true;
    m.tree.push_input(pad);
    EXPECT_FALSE(e->waitingForInput);
}

// --- BriefcaseMachine and the win path ----------------------------------------

scripts::BriefcaseMachine* Brief(MainTree& m) { return Script<scripts::BriefcaseMachine>(m, kBrief); }

// RoundManager.gd L476-L496: clearing batch 3 (index 2 -> 3), not endless: the
// camera to the dealer, +0.7 s the briefcase machine (BriefcaseMachine.gd L26-L42).
TEST_F(EndingTree, ClearingTheLastBatchRollsTheBriefcaseMachineIn) {
    auto* rm = Script<scripts::RoundManager>(m, "standalone managers/round manager");
    scripts::BriefcaseMachine* b = Brief(m);
    ASSERT_NE(rm, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(rm->brief, b);
    rm->playerData->currentBatchIndex = 2;
    m.tree.spawn(rm->EndMainBatch());
    m.Run(1.25);   // .8 + .4, and the lerp starts
    EXPECT_EQ(b->cam->activeSocket, "enemy");
    EXPECT_FALSE(b->parent_machine->is_visible());
    m.Run(0.75);   // + .7: MainRoutine (its sound is not asserted: with no audio
                   // sink is_playing() is true for one frame only, a PORT)
    EXPECT_FALSE(b->parent_eyes->is_visible());
    m.Run(0.45);   // + .42: the eyes
    EXPECT_TRUE(b->parent_eyes->is_visible());
    m.Run(1.3);    // + 1.28: the machine rolls in
    EXPECT_FALSE(b->parent_eyes->is_visible());
    EXPECT_TRUE(b->parent_machine->is_visible());
    EXPECT_EQ(b->anim_machine->get_current_animation(), "move to table");
    EXPECT_FALSE(b->intbranch_L->interactionAllowed);
    m.Run(5.2);    // + 5.14: the camera to the case
    EXPECT_EQ(b->cam->activeSocket, "cash briefcase");
    m.Run(0.85);   // + .8: the latches
    EXPECT_TRUE(b->intbranch_L->interactionAllowed);
    EXPECT_TRUE(b->intbranch_R->interactionAllowed);
    EXPECT_FALSE(b->intbranch_lid->interactionAllowed);
    EXPECT_TRUE(b->btnParent_briefcase->is_visible());
    EXPECT_EQ(b->controller->previousFocus, b->btn_left);
}

// gd: L44-L70 through InteractionManager's aliases: the lid opens
// only after both latches, once.
TEST_F(EndingTree, TheLidOpensOnlyAfterBothLatchesAndStartsTheEndingOnce) {
    scripts::BriefcaseMachine* b = Brief(m);
    auto* im = Script<scripts::InteractionManager>(m, "standalone managers/interaction manager");
    ASSERT_NE(im, nullptr);
    EXPECT_EQ(im->brief, b);
    b->intbranch_L->interactionAllowed = true;
    b->intbranch_R->interactionAllowed = true;
    im->InteractWith("latch left");
    EXPECT_FALSE(b->intbranch_L->interactionAllowed);
    EXPECT_TRUE(b->latchRaisedL);
    EXPECT_EQ(b->anim_latchL->get_current_animation(), "raise");
    m.Run(0.5);
    EXPECT_FALSE(b->intbranch_lid->interactionAllowed) << "one latch is not enough";
    im->InteractWith("latch right");
    EXPECT_EQ(b->anim_latchR->get_current_animation(), "raise");
    m.Run(0.25);
    EXPECT_FALSE(b->intbranch_lid->interactionAllowed);
    m.Run(0.1);
    EXPECT_TRUE(b->intbranch_lid->interactionAllowed) << "+0.3 s after both";
    im->InteractWith("briefcase lid");
    EXPECT_FALSE(b->intbranch_lid->interactionAllowed);
    EXPECT_FALSE(b->btnParent_briefcase->is_visible());
    EXPECT_EQ(b->anim_lid->get_current_animation(), "open");
    EXPECT_EQ(b->ending->animator_pan->get_current_animation(), "pan to brief");
    EXPECT_FALSE(b->ending->exitm->exitAllowed);
}

// GameOverManager.gd L3-L4 (never called in 1.2).
TEST_F(EndingTree, PlayerWonAsksForTheWinScene) {
    auto* g = Script<scripts::GameOverManager>(m, "standalone managers/game over manager");
    ASSERT_NE(g, nullptr);
    g->PlayerWon();
    EXPECT_EQ(m.tree.take_scene_change(), "res://scenes/win.tscn");
}

}  // namespace
