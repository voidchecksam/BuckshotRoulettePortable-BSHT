// Small scripts v2.2 added: StatueManager.gd (the " vellon" statue),
// PipelineManager.gd (the GL path, off in the port) and
// SceneChangeNotification.gd (a log line).
#include "main_tree_fixture.h"
#include "godot/input_event.h"
#include "godot/input_map.h"
#include "godot/print.h"
#include "scripts/ButtonClass_Main.h"
#include "scripts/Debugging.h"
#include "scripts/GlobalVariables.h"
#include "scripts/PipelineManager.h"
#include "scripts/RoundManager.h"
#include "scripts/resources.h"
#include "scripts/SceneChangeNotification.h"
#include "scripts/StatueManager.h"
#include "scripts/TempDeathScreen.h"
#include "scripts/TimeScaleManager.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

struct StatueMain : ::testing::Test {
    testing_support::MainTree m;
    scripts::Statue* statue = nullptr;

    void SetUp() override {
        if (!m.ok) GTEST_SKIP() << "export the main scene first";
        statue = godot::script_cast<scripts::Statue>(m.tree.get_node("standalone managers/statue manager"));
        ASSERT_NE(statue, nullptr);
        ASSERT_NE(statue->rm, nullptr);
        ASSERT_NE(statue->statue, nullptr);
        ASSERT_NE(statue->cup, nullptr);
    }
};

TEST_F(StatueMain, VellonGetsTheStatueInsteadOfTheCup) {  // gd: L7-L10
    // The waiver stores " " + the typed name (SignatureManager.Input_Enter).
    statue->rm->playerData->playername = " vellon";
    statue->CheckStatus();
    EXPECT_TRUE(statue->statue->is_visible());
    EXPECT_FALSE(statue->cup->is_visible());
}

TEST_F(StatueMain, AnyOtherNameLeavesTheCup) {
    const bool statueBefore = statue->statue->is_visible();
    const bool cupBefore = statue->cup->is_visible();
    statue->rm->playerData->playername = " dealer";
    statue->CheckStatus();
    EXPECT_EQ(statue->statue->is_visible(), statueBefore);
    EXPECT_EQ(statue->cup->is_visible(), cupBefore);
}

TEST(PipelineManager, ThePortDrawsLikeVulkanSoNothingIsAdjusted) {  // gd: CheckStatus, AdjustSettings
    scripts::PipelineManager p;
    scripts::GlobalVariables::using_gl = true;
    p.CheckStatus();
    EXPECT_FALSE(scripts::GlobalVariables::using_gl);
    godot::Control overlay("overlay");
    overlay.set_visible(false);
    p.overlay = &overlay;
    p.AdjustSettings("main");
    EXPECT_FALSE(overlay.is_visible()) << "AdjustSettings acts only on the GL path";
}

TEST(SceneChangeNotification, ReadyLogsTheScene) {  // gd: L3-L4
    godot::SceneTree tree;
    if (godot::LoadRuntimeScene(testing_support::Extracted("runtime_menu.json").c_str(), tree) !=
        godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "export runtime_menu.json first";
    }
    scripts::RegisterScripts();
    scripts::BindScripts(tree.get_root());
    std::vector<std::string> lines;
    godot::set_print_sink([&lines](const std::string& s) { lines.push_back(s); });
    tree.ready_scripts();
    godot::set_print_sink(nullptr);
    bool seen = false;
    for (const std::string& l : lines) seen = seen || l == "user entered scene: menu";
    EXPECT_TRUE(seen);
}

}  // namespace

namespace {

// v2.2's ButtonClass_Main shows a 3D bracket with `visible` (v1.2: transparency).
TEST_F(StatueMain, AThreeDBracketShowsAndHidesWithVisible) {
    auto* waiver = godot::script_cast<scripts::ButtonClass_Main>(
        m.tree.get_node("Camera/dialogue UI/bracket ui/waiver pickup/m button_waiver/m class_waiver"));
    ASSERT_NE(waiver, nullptr);
    ASSERT_TRUE(waiver->is3D);
    waiver->SetUI(true);
    EXPECT_TRUE(waiver->ui_3D->is_visible());
    EXPECT_FLOAT_EQ(waiver->ui_3D->get_transparency(), 0.0f);
    waiver->SetUI(false);
    EXPECT_FALSE(waiver->ui_3D->is_visible());
}

}  // namespace

namespace {

// v2.2 logs every scene change: death.tscn's TempDeathScreen goes straight to main.
TEST(TempDeathScreen, TheDeathSceneLogsItsChangeToMain) {
    godot::SceneTree tree;
    godot::Node* node = tree.add_node(std::make_unique<godot::Node>("death screen"), nullptr);
    auto script = std::make_unique<scripts::TempDeathScreen>();
    scripts::TempDeathScreen* screen = script.get();
    node->set_script(std::move(script));
    screen->isDeathScreen = true;
    std::vector<std::string> lines;
    godot::set_print_sink([&lines](const std::string& l) { lines.push_back(l); });
    screen->_ready();
    godot::set_print_sink(nullptr);
    EXPECT_EQ(lines, std::vector<std::string>{"changing scene to: main"});
    EXPECT_EQ(tree.take_scene_change(), "res://scenes/main.tscn");
}

}  // namespace

namespace {

// v2.2 TimeScaleManager._unhandled_input: under mp_debugging the time-scale
// keys stop a running lerp; otherwise nothing happens.
TEST(TimeScaleManager, DebugKeysStopTheLerpOnlyWhileDebugging) {  // gd: L29-L36
    auto& map = godot::InputMap::get_singleton();
    map.clear();
    if (!map.load(BSHT_SOURCE_DIR "/assets/extracted/input_map.json")) GTEST_SKIP() << "export input_map.json first";
    auto minus = std::make_shared<godot::InputEventKey>();
    minus->physical_keycode = 47;   // the "-" action's key in input_map.json
    minus->pressed = true;
    scripts::TimeScaleManager tsm;
    const bool saved = scripts::GlobalVariables::mp_debugging;

    scripts::GlobalVariables::mp_debugging = false;
    tsm.moving = true;
    tsm._unhandled_input(minus);
    EXPECT_TRUE(tsm.moving);

    scripts::GlobalVariables::mp_debugging = true;
    tsm._unhandled_input(minus);
    EXPECT_FALSE(tsm.moving);
    scripts::GlobalVariables::mp_debugging = saved;
}

}  // namespace

namespace {

// v2.2 Debugging.gd: under mp_debugging "," sets time_scale 1 and "." 10,
// both stopping a running lerp; otherwise nothing happens.
TEST(Debug, DebugKeysSetTheTimeScaleOnlyWhileDebugging) {  // gd: L6-L13
    auto& map = godot::InputMap::get_singleton();
    map.clear();
    if (!map.load(BSHT_SOURCE_DIR "/assets/extracted/input_map.json")) GTEST_SKIP() << "export input_map.json first";
    godot::InputEventRef dot;
    for (const godot::InputEventRef& e : map.action_get_events(".")) dot = e;
    ASSERT_NE(dot, nullptr);
    auto press = std::make_shared<godot::InputEventKey>(*std::static_pointer_cast<godot::InputEventKey>(dot));
    press->pressed = true;

    godot::SceneTree tree;
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("main"), nullptr);
    auto debugScript = std::make_unique<scripts::Debug>();
    scripts::Debug* debug = debugScript.get();
    tree.add_node(std::make_unique<godot::Node>("debug"), root)->set_script(std::move(debugScript));
    auto tsmScript = std::make_unique<scripts::TimeScaleManager>();
    scripts::TimeScaleManager* tsm = tsmScript.get();
    tree.add_node(std::make_unique<godot::Node>("time scale"), root)->set_script(std::move(tsmScript));
    debug->timescale = tsm;
    const bool saved = scripts::GlobalVariables::mp_debugging;

    scripts::GlobalVariables::mp_debugging = false;
    tsm->moving = true;
    debug->_unhandled_input(press);
    EXPECT_DOUBLE_EQ(tree.get_time_scale(), 1.0);
    EXPECT_TRUE(tsm->moving);

    scripts::GlobalVariables::mp_debugging = true;
    debug->_unhandled_input(press);
    EXPECT_DOUBLE_EQ(tree.get_time_scale(), 10.0);
    EXPECT_FALSE(tsm->moving);
    scripts::GlobalVariables::mp_debugging = saved;
}

}  // namespace
