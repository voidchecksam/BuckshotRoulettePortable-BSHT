// The restroom intro and its small scripts, on the real main tree.
#include "main_tree_fixture.h"

#include "godot/control.h"
#include "game/achievements.h"
#include "game/save_store.h"
#include "godot/animation_player.h"
#include "godot/input_event.h"
#include "godot/input_map.h"
#include "godot/translation.h"
#include "scripts/AchievementManager.h"
#include "scripts/BlockoutManager.h"
#include "scripts/BpmLight.h"
#include "scripts/DonUnlockManager.h"
#include "scripts/InteractionBranch.h"
#include "game/session.h"
#include "scripts/IntroManager.h"
#include "scripts/RoundManager.h"
#include "scripts/StatueManager.h"
#include "scripts/resources.h"
#include "scripts/JankPreventer.h"
#include "scripts/ResetManager.h"
#include "scripts/SaveFileManager.h"
#include "scripts/SignatureButtonBranch.h"
#include "scripts/SignatureManager.h"
#include "scripts/intro_skip.h"

#include "print_capture.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

using testing_support::MainTree;

// The intro scripts bind from the real export.
TEST(IntroBindings, IntroManagerAndSignatureBindTheirExports) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    for (const std::string& e : m.bindErrors) ADD_FAILURE() << e;
    auto* intro = godot::script_cast<scripts::IntroManager>(m.tree.get_node("standalone managers/intro manager"));
    ASSERT_NE(intro, nullptr);
    EXPECT_NE(intro->intbranch_bathroomdoor, nullptr);
    EXPECT_NE(intro->animator_camera, nullptr);
    EXPECT_NE(intro->viewblocker, nullptr);
    EXPECT_NE(intro->roundManager, nullptr);
    auto* sign = godot::script_cast<scripts::Signature>(m.tree.get_node("standalone managers/signature manager"));
    ASSERT_NE(sign, nullptr);
    EXPECT_EQ(sign->letterArray.size(), 6u);
}

// The one way anything skips the intro (dump_state.gd does the same).
TEST(IntroBindings, SkipIntroFreesTheManagerAndLiftsTheBlackout) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    auto* blocker = m.tree.get_node_as<godot::Control>("Camera/dialogue UI/viewblocker parent/viewblcoker");
    ASSERT_NE(blocker, nullptr);
    ASSERT_TRUE(blocker->is_visible()) << "the scene starts blacked out, inside the intro";
    scripts::SkipIntro(m.tree);
    m.Run(1.0 / 60.0);
    EXPECT_EQ(m.tree.get_node("standalone managers/intro manager"), nullptr);
    EXPECT_FALSE(blocker->is_visible());
}

// --- the small intro scripts --------------------------------------------------

std::string TestDir(const char* name) {
    return std::string(BSHT_BINARY_DIR) + "/intro_test_" + name + "_" +
           ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

template <class T>
T* FirstScript(godot::SceneTree& tree) {
    for (godot::Node* n : tree.nodes_in_tree_order()) {
        if (auto* s = godot::script_cast<T>(n)) return s;
    }
    return nullptr;
}

// gd: DonUnlockManager.gd L22-L44. The pills file is SaveStore entry "pills".
TEST(Unlocker, AFreshSaveHasNoUnlockAndTheTenthPillUnlocksAch4) {
    const std::string dir = TestDir("unlocker");
    game::SaveStore store(dir);
    store.Clear(scripts::Unlocker::savepath);
    game::Achievements achievements;
    ASSERT_TRUE(achievements.LoadText((std::string(BSHT_SOURCE_DIR) + "/tools/achievements_en.json").c_str()));
    achievements.Attach(nullptr);
    scripts::Achievement ach;
    ach.store = &achievements;
    scripts::Unlocker u;
    u.ach = &ach;
    u.store = &store;
    EXPECT_FALSE(u.Unlocked());
    u.IncrementAmount();                     // no file: nothing happens
    EXPECT_FALSE(u.Unlocked());
    u.UnlockMode();
    EXPECT_TRUE(u.Unlocked());
    for (int i = 0; i < 9; ++i) u.IncrementAmount();
    EXPECT_FALSE(achievements.IsUnlocked("ach4"));
    u.IncrementAmount();                     // the tenth
    EXPECT_TRUE(achievements.IsUnlocked("ach4"));
}

TEST(Unlocker, WithNoStoreNothingIsUnlocked) {   // a diagnostic boot
    scripts::Unlocker u;
    EXPECT_FALSE(u.Unlocked());
    u.UnlockMode();
    EXPECT_FALSE(u.Unlocked());
}

TEST(Blockout, HideClubHidesEveryClubObject) {   // gd: L5-L7
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    auto* b = FirstScript<scripts::Blockout>(m.tree);
    ASSERT_NE(b, nullptr);
    ASSERT_FALSE(b->objArray_club.empty());
    b->HideClub();
    for (godot::Node3D* n : b->objArray_club) EXPECT_FALSE(n->is_visible());
}

TEST(SignButton, APressMovesTheButtonDownAndBackUp) {   // gd: L20-L41
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    scripts::SkipIntro(m.tree);
    auto* btn = FirstScript<scripts::SignButton>(m.tree);
    ASSERT_NE(btn, nullptr);
    ASSERT_NE(btn->parent, nullptr);
    ASSERT_NE(btn->intbranch, nullptr);
    m.tree.spawn(btn->Press(), btn->get_node_owner());
    EXPECT_FALSE(btn->intbranch->interactionAllowed);
    m.Run(2.0 / 60.0);
    EXPECT_LT(btn->parent->get_position().y, btn->y_up - 1e-5);
    m.Run(0.5);
    // Back up, but, as in the original, not exactly at y_up: the lerp never
    // clamps and stops when the 0.06 s timer ends (the export's rest y is
    // -1.22763 against y_up -1.228 for the same reason).
    EXPECT_GT(btn->parent->get_position().y, 0.5 * (btn->y_up + btn->y_down));
    EXPECT_TRUE(btn->intbranch->interactionAllowed);
}

// gd: L6-L15 (isHeaven false): JA and ES hide the counter's arrows after 2 s.
TEST(JankPreventer, JapaneseHidesTheCounterArrowsEnglishKeepsThem) {
    for (const char* locale : {"JA", "EN"}) {
        MainTree m;
        if (!m.ok) GTEST_SKIP() << "exports missing";
        godot::set_locale(locale);
        m.tree.ready_scripts();
        scripts::SkipIntro(m.tree);
        auto* j = FirstScript<scripts::JankPreventer>(m.tree);
        ASSERT_NE(j, nullptr);
        // The counter's `lines2` MeshInstance3D.
        auto* parent = godot::cast_to<godot::Node3D>(j->get_node_owner()->get_parent());
        ASSERT_NE(parent, nullptr);
        m.Run(1.9);
        EXPECT_TRUE(parent->is_visible()) << locale << ": hidden before the second second";
        const godot::Vector3 before = j->text_dealer->get_scale();
        m.Run(0.3);
        EXPECT_EQ(parent->is_visible(), std::string(locale) != "JA") << locale;
        // v2.2: JA also stretches the shooting choice's DEALER text.
        const godot::Vector3 after = j->text_dealer->get_scale();
        if (std::string(locale) == "JA") {
            EXPECT_FLOAT_EQ(after.x, 1.409f);
            EXPECT_FLOAT_EQ(after.y, 2.466f);
            EXPECT_FLOAT_EQ(after.z, 1.752f);
        } else {
            EXPECT_FLOAT_EQ(after.y, before.y) << locale;
        }
    }
    godot::set_locale("EN");
}

struct ResetTree {
    MainTree m;
    game::SaveStore store;
    scripts::ResetManager* rm = nullptr;

    explicit ResetTree(const std::string& dir) : store(dir) {
        if (!m.ok) return;
        game::SaveMap data{{"x", true}};
        EXPECT_TRUE(store.Save(scripts::SaveFileManager::savePath, data));
        rm = FirstScript<scripts::ResetManager>(m.tree);
        if (!rm || !rm->save) return;
        rm->save->store = &store;
        m.tree.ready_scripts();
        scripts::SkipIntro(m.tree);
    }
    bool Saved() { return store.Exists(scripts::SaveFileManager::savePath); }
};

// The map's events are templates (pressed = false): R is the key "reset" binds.
godot::InputEventRef ResetKey(bool pressed) {
    auto key = std::make_shared<godot::InputEventKey>();
    key->keycode = 82;
    key->pressed = pressed;
    return key;
}

// v2.2 (gd: L8-L30): reset is a 0.8 s hold, timed in _process; then the save
// is cleared and the death scene loads, once.
TEST(ResetManager, HoldingResetForPointEightSecondsClearsTheSaveAndDiesOnce) {
    ResetTree r(TestDir("reset"));
    if (!r.m.ok) GTEST_SKIP() << "exports missing";
    ASSERT_NE(r.rm, nullptr);
    testing_support::PrintCapture log;
    ASSERT_TRUE(ResetKey(true)->is_action_pressed("reset"));
    r.m.tree.push_input(ResetKey(true));
    r.m.Run(0.7);
    EXPECT_TRUE(r.Saved()) << "not before 0.8 s";
    EXPECT_EQ(r.m.tree.take_scene_change(), "");
    r.m.Run(0.15);
    EXPECT_FALSE(r.Saved());
    EXPECT_EQ(r.m.tree.take_scene_change(), "res://scenes/death.tscn");
    EXPECT_TRUE(log.Has("changing scene to: death"));
    r.m.tree.push_input(ResetKey(true));
    r.m.Run(1.0);
    EXPECT_EQ(r.m.tree.take_scene_change(), "") << "fs: once";
}

TEST(ResetManager, ReleasingBeforePointEightSecondsStartsTheHoldAgain) {
    ResetTree r(TestDir("reset_release"));
    if (!r.m.ok) GTEST_SKIP() << "exports missing";
    ASSERT_NE(r.rm, nullptr);
    r.m.tree.push_input(ResetKey(true));
    r.m.Run(0.5);
    r.m.tree.push_input(ResetKey(false));
    r.m.Run(0.5);
    r.m.tree.push_input(ResetKey(true));
    r.m.Run(0.5);
    EXPECT_TRUE(r.Saved());
    EXPECT_EQ(r.m.tree.take_scene_change(), "");
}

// PORT: on the Vita the pad's reset (Triangle) is off; R still resets.
TEST(ResetManager, ThePadResetsOnlyWhereItIsAllowed) {
    for (const bool padResets : {true, false}) {
        ResetTree r(TestDir(padResets ? "reset_pad_on" : "reset_pad_off"));
        if (!r.m.ok) GTEST_SKIP() << "exports missing";
        ASSERT_NE(r.rm, nullptr);
        r.rm->padResets = padResets;
        auto triangle = std::make_shared<godot::InputEventJoypadButton>();
        triangle->button_index = 3;
        triangle->pressed = true;
        ASSERT_TRUE(triangle->is_action_pressed("reset"));
        r.m.tree.push_input(triangle);
        r.m.Run(0.9);
        EXPECT_EQ(r.Saved(), !padResets) << padResets;
        EXPECT_EQ(r.m.tree.take_scene_change(), padResets ? "res://scenes/death.tscn" : "") << padResets;
    }
}

// gd: L11-L26: the club light hits on the track's beat.
TEST(BpmLight, TheMainLoopHitsEveryDelay) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    scripts::SkipIntro(m.tree);
    auto* b = FirstScript<scripts::BpmLight>(m.tree);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(b->animator, nullptr);
    b->delay = 0.5;
    b->BeginMainLoop();
    EXPECT_EQ(b->animator->get_current_animation(), "hit");
    EXPECT_TRUE(b->looping);
}

// --- IntroManager -------------------------------------------------------------

scripts::IntroManager* Intro(MainTree& m) {
    return godot::script_cast<scripts::IntroManager>(m.tree.get_node("standalone managers/intro manager"));
}

// Steps frames until the dealer starts walking back to the table (0.5 s after
// RoundManager.BeginMainGame), or `seconds` pass. True when he did.
bool RunUntilDealerReturns(MainTree& m, double seconds) {
    auto* rm = godot::script_cast<scripts::RoundManager>(m.tree.get_node("standalone managers/round manager"));
    const int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i) {
        m.tree.process_frame(1.0 / 60.0);
        if (rm && rm->animator_dealer && rm->animator_dealer->get_current_animation() == "dealer return to table") {
            return true;
        }
    }
    return false;
}

// gd: L47-L98: a fresh boot. The blackout lifts at +1.0 s (0.5 _ready + 0.5),
// the door opens 3 s later; no pills without the unlock.
TEST(Intro, AFreshBootLiftsTheBlackoutAndOpensTheBathroomDoor) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    auto* intro = Intro(m);
    ASSERT_NE(intro, nullptr);
    auto* blocker = intro->viewblocker;
    ASSERT_NE(blocker, nullptr);
    m.Run(0.9);
    EXPECT_TRUE(blocker->is_visible());
    m.Run(0.2);
    EXPECT_FALSE(blocker->is_visible());
    EXPECT_FALSE(intro->intbranch_bathroomdoor->interactionAllowed);
    m.Run(3.1);
    EXPECT_TRUE(intro->intbranch_bathroomdoor->interactionAllowed);
    EXPECT_TRUE(intro->btn_bathroomdoor->is_visible());
    EXPECT_FALSE(intro->parent_pills->is_visible());
    EXPECT_FALSE(intro->intbranch_pillbottle->interactionAllowed);
    EXPECT_EQ(intro->animator_camera->get_current_animation(), "camera idle bathroom");
}

// gd: L282-L299: the door walks once and opens the backroom door 5 s later.
TEST(Intro, TheBathroomDoorWalksOnceAndOpensTheBackroomDoor) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    auto* intro = Intro(m);
    ASSERT_NE(intro, nullptr);
    m.Run(4.5);
    m.tree.spawn(intro->Interaction_BathroomDoor(), intro->get_node_owner());
    EXPECT_FALSE(intro->intbranch_bathroomdoor->interactionAllowed) << "a second press must be gated";
    EXPECT_EQ(intro->animator_camera->get_current_animation(), "camera exit bathroom");
    m.Run(4.9);
    EXPECT_FALSE(intro->intbranch_backroomdoor->interactionAllowed);
    m.Run(0.2);
    EXPECT_TRUE(intro->intbranch_backroomdoor->interactionAllowed);
    EXPECT_TRUE(intro->btn_backroom->is_visible());
    EXPECT_TRUE(intro->counting);
}

// gd: L274-L303 + the METHOD keys of "camera enter backroom" (KickDoorLobby,
// PanFilter, BeginGame): the walk ends in RoundManager.BeginMainGame.
TEST(Intro, TheBackroomWalkEndsInBeginMainGame) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    auto* intro = Intro(m);
    ASSERT_NE(intro, nullptr);
    m.Run(4.5);
    m.tree.spawn(intro->Interaction_BathroomDoor(), intro->get_node_owner());
    m.Run(5.2);
    intro->Interaction_BackroomDoor();
    EXPECT_FALSE(intro->counting);
    EXPECT_EQ(intro->animator_camera->get_current_animation(), "camera enter backroom");
    EXPECT_TRUE(RunUntilDealerReturns(m, 30.0)) << "BeginGame never started the batch";
    EXPECT_EQ(intro->roundManager->playerData->stat_doorsKicked, 2);
    ASSERT_FALSE(intro->blockout->objArray_club.empty());
    EXPECT_FALSE(intro->blockout->objArray_club.front()->is_visible());
    EXPECT_GT(m.tree.animation_stats().methods_called, 0);
    EXPECT_EQ(m.tree.animation_stats().methods_unhandled, 0);
}

// gd: L59-L62, L96: with the Double or Nothing unlock the pills and the CRT
// appear.
TEST(Intro, WithTheUnlockThePillsAppear) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    const std::string dir = TestDir("intro_unlock");
    game::SaveStore store(dir);
    auto* intro = Intro(m);
    ASSERT_NE(intro, nullptr);
    ASSERT_NE(intro->unlocker, nullptr);
    intro->unlocker->store = &store;
    intro->unlocker->UnlockMode();
    m.tree.ready_scripts();
    m.Run(4.5);
    EXPECT_TRUE(intro->parent_pills->is_visible());
    EXPECT_TRUE(intro->intbranch_pillbottle->interactionAllowed);
    EXPECT_TRUE(intro->btn_pills->is_visible());
    store.Clear(scripts::Unlocker::savepath);
}

// --- Session's two paths ------------------------------------------------------

// A player's boot: Session leaves the tree to IntroManager, still blacked out,
// the waiver unsigned, the intro manager alive, no batch started.
TEST(IntroSession, APlayersBootLeavesTheTreeToTheIntro) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    game::Session session;
    session.SetPlaysIntro(true);
    ASSERT_TRUE(session.Start(&m.tree));
    m.Run(1.0 / 60.0);
    auto* intro = Intro(m);
    ASSERT_NE(intro, nullptr);
    EXPECT_TRUE(intro->viewblocker->is_visible());
    EXPECT_FALSE(intro->roundManager->playerData->hasSignedWaiver);
    EXPECT_FALSE(RunUntilDealerReturns(m, 2.0)) << "the batch must wait for the walk";
}

// A diagnostic: the intro skipped, the batch begins.
TEST(IntroSession, ADiagnosticSkipsTheIntroAndStartsTheBatch) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    game::Session session;
    session.SetPlaysIntro(false);
    ASSERT_TRUE(session.Start(&m.tree));
    m.Run(1.0 / 60.0);
    EXPECT_EQ(m.tree.get_node("standalone managers/intro manager"), nullptr);
    EXPECT_TRUE(RunUntilDealerReturns(m, 2.0));
}

// --- the revival --------------------------------------------------------------

// gd: L47-L57, L106-L150: back from a death, the smoker's revival, then the
// same walk; the batch, the name and the signature survive, no second waiver.
TEST(IntroRevival, ADeathPlaysTheRevivalAndKeepsThePlayer) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    auto* intro = Intro(m);
    ASSERT_NE(intro, nullptr);
    scripts::PlayerData* pd = intro->roundManager->playerData;
    pd->playerEnteringFromDeath = true;
    pd->hasSignedWaiver = true;
    pd->currentBatchIndex = 1;
    pd->playername = "abe";
    m.tree.ready_scripts();

    m.Run(0.6);   // the 0.5 s _ready await: RevivalBathroomStart begins
    EXPECT_TRUE(intro->smokerdude_revival->is_visible());
    const godot::Vector3 cam = intro->cameraParent->get_position();
    EXPECT_NEAR(cam.x, intro->pos_floor.x, 1e-3);
    EXPECT_NEAR(cam.y, intro->pos_floor.y, 1e-3);
    m.Run(2.0);   // +0.85 s: the defib, the blackout lifted
    EXPECT_FALSE(intro->viewblocker->is_visible());
    EXPECT_FALSE(intro->intbranch_bathroomdoor->interactionAllowed);
    m.Run(14.0);  // the lines, the fade, 3 s: 15.9 s in all
    EXPECT_FALSE(intro->smokerdude_revival->is_visible());
    EXPECT_TRUE(intro->intbranch_bathroomdoor->interactionAllowed);
    EXPECT_FALSE(intro->parent_pills->is_visible()) << "no pills when entering from death";

    m.tree.spawn(intro->Interaction_BathroomDoor(), intro->get_node_owner());
    m.Run(5.2);
    intro->Interaction_BackroomDoor();
    EXPECT_TRUE(RunUntilDealerReturns(m, 30.0));
    auto* sign = godot::script_cast<scripts::Signature>(m.tree.get_node("standalone managers/signature manager"));
    ASSERT_NE(sign, nullptr);
    m.Run(8.0);
    EXPECT_FALSE(sign->intrbranch_waiver->interactionAllowed) << "a signed player is never asked again";
    EXPECT_EQ(pd->currentBatchIndex, 1);
    EXPECT_EQ(pd->playername, "abe");
}

}  // namespace


namespace {

// v2.2: kicking the backroom door checks the " vellon" statue (StatueManager).
TEST(IntroV22, TheBackroomDoorChecksTheStatue) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "export the main scene first";
    auto* intro = godot::script_cast<scripts::IntroManager>(m.tree.get_node("standalone managers/intro manager"));
    ASSERT_NE(intro, nullptr);
    ASSERT_NE(intro->statue, nullptr);
    intro->roundManager->playerData->playername = " vellon";
    intro->Interaction_BackroomDoor();
    EXPECT_TRUE(intro->statue->statue->is_visible());
    EXPECT_FALSE(intro->statue->cup->is_visible());
}

}  // namespace
