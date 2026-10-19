// The waiver: SignatureManager.gd on the real main tree. gd line numbers are
// SignatureManager.gd's.
#include "main_tree_fixture.h"

#include "game/achievements.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "scripts/AchievementManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/RoundManager.h"
#include "scripts/SignatureManager.h"
#include "scripts/intro_skip.h"
#include "scripts/resources.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using testing_support::MainTree;

struct Waiver {
    MainTree m;
    game::Achievements achievements;
    scripts::Signature* sign = nullptr;
    scripts::RoundManager* roundManager = nullptr;
    bool ok = false;

    // The waiver machine is up and the letter buttons live (PickUpWaiver done).
    Waiver() {
        if (!m.ok) return;
        achievements.LoadText((std::string(BSHT_SOURCE_DIR) + "/tools/achievements_en.json").c_str());
        achievements.Attach(nullptr);
        m.tree.ready_scripts();
        scripts::SkipIntro(m.tree);
        sign = godot::script_cast<scripts::Signature>(m.tree.get_node("standalone managers/signature manager"));
        roundManager = godot::script_cast<scripts::RoundManager>(m.tree.get_node("standalone managers/round manager"));
        if (!sign || !roundManager || !sign->ach) return;
        sign->ach->store = &achievements;
        m.tree.spawn(sign->PickUpWaiver(), sign->get_node_owner());
        m.Run(3.0);
        ok = true;
    }
    void Type(const std::string& letters) {
        for (char c : letters) sign->GetInput(std::string(1, c), "");
    }
    std::string Joined() const {
        std::string s;
        for (godot::Label3D* l : sign->letterArray) s += l->get_text();
        return s;
    }
    void Enter() { m.tree.spawn(sign->Input_Enter(), sign->get_node_owner()); }
};

TEST(Waiver, PickingItUpOpensTheLetterButtons) {   // gd: L62-L79
    Waiver w;
    if (!w.ok) GTEST_SKIP() << "exports missing";
    EXPECT_TRUE(w.sign->parent_signatureMachineMainParent->is_visible());
    ASSERT_FALSE(w.sign->intbranches.empty());
    for (scripts::InteractionBranch* b : w.sign->intbranches) EXPECT_TRUE(b->interactionAllowed);
    EXPECT_EQ(w.sign->markerIndex, 0);
}

TEST(Waiver, SixLettersMaxAndBackspaceAtBothEnds) {   // gd: L94-L99, L152-L158
    Waiver w;
    if (!w.ok) GTEST_SKIP() << "exports missing";
    w.Type("abcdefg");
    EXPECT_EQ(w.Joined(), "abcdef") << "the 7th letter is refused";
    EXPECT_EQ(w.sign->markerIndex, 6);
    EXPECT_FALSE(w.sign->marker->is_visible());
    w.sign->Input_Backspace();
    EXPECT_EQ(w.Joined(), "abcde");
    EXPECT_EQ(w.sign->markerIndex, 5);
    for (int i = 0; i < 7; ++i) w.sign->Input_Backspace();
    EXPECT_EQ(w.Joined(), "");
    EXPECT_EQ(w.sign->markerIndex, 0);
    EXPECT_TRUE(w.sign->marker->is_visible());
}

TEST(Waiver, EnterOnNothingDoesNothing) {   // gd: L111
    Waiver w;
    if (!w.ok) GTEST_SKIP() << "exports missing";
    w.Enter();
    w.m.Run(6.0);
    EXPECT_FALSE(w.roundManager->playerData->hasSignedWaiver);
}

TEST(Waiver, DealerIsRefusedAndGodUnlocksAch11) {   // gd: L112-L115
    Waiver w;
    if (!w.ok) GTEST_SKIP() << "exports missing";
    w.Type("dealer");
    w.Enter();
    w.m.Run(6.0);
    EXPECT_FALSE(w.roundManager->playerData->hasSignedWaiver);
    for (int i = 0; i < 6; ++i) w.sign->Input_Backspace();
    w.Type("god");
    w.Enter();
    w.m.Run(6.0);
    EXPECT_TRUE(w.achievements.IsUnlocked("ach11"));
    EXPECT_FALSE(w.roundManager->playerData->hasSignedWaiver);
}

TEST(Waiver, AnAcceptedNameSignsTheWaiverAndReturnsToTheBatch) {   // gd: L116-L150
    Waiver w;
    if (!w.ok) GTEST_SKIP() << "exports missing";
    w.Type("balls");
    w.Enter();
    for (scripts::InteractionBranch* b : w.sign->intbranches) EXPECT_FALSE(b->interactionAllowed);
    w.m.Run(8.0);
    EXPECT_TRUE(w.roundManager->playerData->hasSignedWaiver);
    EXPECT_EQ(w.roundManager->playerData->playername, " balls") << "v2.2 stores a leading space";
    EXPECT_EQ(w.sign->letterArray_signature_joined[0]->get_text(), "B");
    EXPECT_EQ(w.sign->letterArray_signature_separate[4]->get_text(), "S");
    EXPECT_FALSE(w.sign->parent_signatureMachineMainParent->is_visible());
    EXPECT_TRUE(w.roundManager->counting);
    auto* body = godot::cast_to<godot::CollisionObject3D>(
        w.sign->intbranches.front()->get_node_owner()->get_parent()->get_child(2));
    ASSERT_NE(body, nullptr);
    EXPECT_FALSE(body->get_collision_layer_value(1));
}

// RoundManager's `sign.AwaitPickup()` (RoundManager.gd, MainBatchSetup): an
// unsigned batch puts the release form in reach.
TEST(Waiver, AnUnsignedBatchOffersTheReleaseForm) {
    MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    scripts::SkipIntro(m.tree);
    auto* sign = godot::script_cast<scripts::Signature>(m.tree.get_node("standalone managers/signature manager"));
    auto* rm = godot::script_cast<scripts::RoundManager>(m.tree.get_node("standalone managers/round manager"));
    ASSERT_NE(sign, nullptr);
    ASSERT_NE(rm, nullptr);
    ASSERT_FALSE(rm->playerData->hasSignedWaiver);
    rm->BeginMainGame();
    m.Run(8.0);
    EXPECT_TRUE(sign->intrbranch_waiver->interactionAllowed);
    EXPECT_TRUE(sign->btn_waiver->is_visible());
}

}  // namespace
