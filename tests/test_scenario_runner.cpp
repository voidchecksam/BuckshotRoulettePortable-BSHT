// app::ScenarioRunner on the menu scene: a scenario whose "scene" is "menu"
// seeds the global RNG at its start frame, as dump_state.gd's _step_menu does,
// and reaches its gate without a round manager in the tree.
#include "app/scenario_runner.h"
#include "assets/animation.h"
#include "godot/input.h"
#include "godot/rng.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"
#include "scripts/BriefcaseMachine.h"
#include "scripts/bind_support.h"

#include <string>

#include <gtest/gtest.h>

namespace {

const std::string kData = std::string(BSHT_SOURCE_DIR) + "/assets/extracted";

const char* kMenuScenario = R"({
 "name": "menu_test",
 "scene": "menu",
 "seed": 20260928,
 "start_frame": 1,
 "wait_for_visible": "Camera/dialogue UI/menu ui/main screen",
 "after": 10,
 "timeline": []
})";

TEST(ScenarioRunner, TheSceneFieldDefaultsToMain) {
    app::ScenarioRunner runner;
    ASSERT_TRUE(runner.Parse(R"({"name": "x", "wait_for_visible": "a"})"));
    EXPECT_EQ(runner.scene(), "main");
    ASSERT_TRUE(runner.Parse(kMenuScenario));
    EXPECT_EQ(runner.scene(), "menu");
}

TEST(ScenarioRunner, AMenuScenarioSeedsTheRngAndReachesItsGate) {
    godot::SceneTree tree;
    if (godot::LoadRuntimeScene((kData + "/runtime_menu.json").c_str(), tree) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "export runtime_menu.json first";
    }
    scripts::RegisterScripts();
    scripts::BindScripts(tree.get_root());
    tree.ready_scripts();

    app::ScenarioRunner runner;
    ASSERT_TRUE(runner.Parse(kMenuScenario));
    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    godot::seed(1);
    ASSERT_EQ(runner.BeginFrame(tree, input), app::ScenarioRunner::Step::Seeded);
    const int64_t drawn = godot::randi();
    godot::seed(20260928);
    EXPECT_EQ(drawn, godot::randi()) << "the start frame must seed(20260928), as _step_menu does";

    ASSERT_TRUE(runner.EndFrame(input));
    EXPECT_EQ(runner.BeginFrame(tree, input), app::ScenarioRunner::Step::Running);
    EXPECT_TRUE(runner.gateReached());
}

}  // namespace

#include "main_tree_fixture.h"
#include "scripts/RoundManager.h"
#include "scripts/resources.h"

namespace {

// An "intro" scenario runs main.tscn with the intro not skipped and sets the
// PlayerData fields it names, as dump_state.gd's _step_intro does at
// start_frame.
TEST(ScenarioRunner, AnIntroScenarioKeepsTheIntroAndSeedsThePlayer) {
    testing_support::MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    app::ScenarioRunner runner;
    ASSERT_TRUE(runner.Parse(R"({"name": "intro_x", "scene": "intro", "seed": 5, "start_frame": 1,
        "wait_for_visible": "Camera/dialogue UI/viewblocker parent/viewblcoker", "after": 5, "timeline": [],
        "player": {"entering_from_death": true, "signed": true, "name": "abe", "batch": 1}})"));
    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    ASSERT_EQ(runner.BeginFrame(m.tree, input), app::ScenarioRunner::Step::Seeded);
    m.tree.process_frame(1.0 / 60.0);
    EXPECT_NE(m.tree.get_node("standalone managers/intro manager"), nullptr) << "the intro must run";
    auto* rm = godot::script_cast<scripts::RoundManager>(m.tree.get_node("standalone managers/round manager"));
    ASSERT_NE(rm, nullptr);
    EXPECT_TRUE(rm->playerData->playerEnteringFromDeath);
    EXPECT_TRUE(rm->playerData->hasSignedWaiver);
    EXPECT_EQ(rm->playerData->playername, "abe");
    EXPECT_EQ(rm->playerData->currentBatchIndex, 1);
}

// A "waiver" scenario on main starts at the table with the waiver unsigned.
TEST(ScenarioRunner, AWaiverScenarioLeavesTheWaiverUnsigned) {
    testing_support::MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    app::ScenarioRunner runner;
    ASSERT_TRUE(runner.Parse(R"({"name": "waiver_x", "scene": "main", "seed": 5, "start_frame": 1, "waiver": true,
        "wait_for_visible": "Camera/dialogue UI/bracket ui/waiver pickup/m button_waiver", "after": 5, "timeline": []})"));
    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    ASSERT_EQ(runner.BeginFrame(m.tree, input), app::ScenarioRunner::Step::Seeded);
    auto* rm = godot::script_cast<scripts::RoundManager>(m.tree.get_node("standalone managers/round manager"));
    ASSERT_NE(rm, nullptr);
    EXPECT_FALSE(rm->playerData->hasSignedWaiver);
}

// An "ending" scenario starts at the table with the player's stats, then
// clears the last batch (EndMainBatch at index 2): dump_state.gd's
// _step_ending twin.
TEST(ScenarioRunner, AnEndingScenarioClearsTheLastBatch) {
    testing_support::MainTree m;
    if (!m.ok) GTEST_SKIP() << "exports missing";
    m.tree.ready_scripts();
    app::ScenarioRunner runner;
    ASSERT_TRUE(runner.Parse(R"({"name": "ending_x", "scene": "ending", "seed": 5, "start_frame": 1,
        "wait_for_visible": "Camera/dialogue UI/bracket ui/money briefcase", "after": 5, "timeline": [],
        "player": {"name": "ABE", "shots": 7, "shells": 9, "doors": 3, "cigs": 2, "beer": 660}})"));
    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    ASSERT_EQ(runner.BeginFrame(m.tree, input), app::ScenarioRunner::Step::Seeded);
    auto* rm = godot::script_cast<scripts::RoundManager>(m.tree.get_node("standalone managers/round manager"));
    ASSERT_NE(rm, nullptr);
    EXPECT_EQ(rm->playerData->playername, "ABE");
    EXPECT_EQ(rm->playerData->stat_beerDrank, 660);
    EXPECT_EQ(rm->playerData->currentBatchIndex, 3) << "set to 2; EndMainBatch adds one before it awaits";
    m.tree.process_frame(1.0 / 60.0);
    EXPECT_EQ(m.tree.get_node("standalone managers/intro manager"), nullptr) << "the intro is skipped";
    for (int i = 0; i < 60 * 10; ++i) m.tree.process_frame(1.0 / 60.0);
    auto* brief = godot::script_cast<scripts::BriefcaseMachine>(m.tree.get_node("standalone managers/briefcase machine"));
    ASSERT_NE(brief, nullptr);
    EXPECT_TRUE(brief->parent_machine->is_visible());
    EXPECT_TRUE(brief->btnParent_briefcase->is_visible()) << "the gate opens";
}

}  // namespace
