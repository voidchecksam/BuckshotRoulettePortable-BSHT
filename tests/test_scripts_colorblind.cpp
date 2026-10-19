// v2.2's colorblind option: ColorblindManager.gd recolours the shells' shared
// live/blank materials, ChecmarkBranch.gd swaps the options checkmark.
#include "main_tree_fixture.h"
#include "godot/control.h"
#include "scripts/ChecmarkBranch.h"
#include "scripts/ColorblindManager.h"
#include "scripts/GlobalVariables.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/ShellExamine.h"

#include <gtest/gtest.h>

namespace {

using scripts::GlobalVariables;
using testing_support::Extracted;

struct ColorblindMain : ::testing::Test {
    testing_support::MainTree m;
    scripts::Colorblind* colorblind = nullptr;
    bool saved = GlobalVariables::colorblind;

    void SetUp() override {
        if (!m.ok) GTEST_SKIP() << "export the main scene first";
        colorblind = godot::script_cast<scripts::Colorblind>(m.tree.get_node("standalone managers/colorblind manager"));
        ASSERT_NE(colorblind, nullptr);
    }
    void TearDown() override { GlobalVariables::colorblind = saved; }
};

TEST_F(ColorblindMain, OffItPaintsTheDefaultShellColours) {  // gd: L7-L9
    GlobalVariables::colorblind = false;
    colorblind->_ready();
    EXPECT_FLOAT_EQ(colorblind->mat_live->albedo.g, 0.28f);
    EXPECT_FLOAT_EQ(colorblind->mat_blank->albedo.r, 0.29f);
}

TEST_F(ColorblindMain, OnItPaintsWhiteAndGreyIntoTheMaterialEveryShellShares) {
    GlobalVariables::colorblind = true;
    colorblind->_ready();
    EXPECT_FLOAT_EQ(colorblind->mat_live->albedo.g, 1.0f);
    EXPECT_FLOAT_EQ(colorblind->mat_blank->albedo.r, 0.34f);
    auto* examine = godot::script_cast<scripts::ShellExamine>(m.tree.get_node("standalone managers/shell examine"));
    ASSERT_NE(examine, nullptr);
    EXPECT_EQ(examine->mat_live, colorblind->mat_live) << "the same .tres, the same material";
}

// v2.2's ShellEjectManager._ready colours its own live/blank materials (each
// ejector has embedded, fading ones): the colour-blind pair when the option
// is on, the default pair otherwise.
TEST_F(ColorblindMain, TheShellEjectorPaintsItsMaterialsAtReady) {  // gd: L16-L19
    auto* ejector = godot::script_cast<scripts::ShellEjectManager>(
        m.tree.get_node("shell parent_ejecting player side/shell eject manager"));
    ASSERT_NE(ejector, nullptr);
    GlobalVariables::colorblind = true;
    ejector->_ready();
    EXPECT_FLOAT_EQ(ejector->mat_live->albedo.g, GlobalVariables::colorblind_color_live.g);
    EXPECT_FLOAT_EQ(ejector->mat_blank->albedo.r, GlobalVariables::colorblind_color_blank.r);
    GlobalVariables::colorblind = false;
    ejector->_ready();
    EXPECT_FLOAT_EQ(ejector->mat_live->albedo.g, GlobalVariables::default_color_live.g);
    EXPECT_FLOAT_EQ(ejector->mat_blank->albedo.r, GlobalVariables::default_color_blank.r);
}

TEST(Checkmark, UpdateCheckmarkShowsTheTickedOrEmptyBox) {  // gd: L7-L9
    godot::SceneTree tree;
    if (godot::LoadRuntimeScene(Extracted("runtime_menu.json").c_str(), tree) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "export runtime_menu.json first";
    }
    scripts::RegisterScripts();
    scripts::BindScripts(tree.get_root());
    auto* mark = godot::script_cast<scripts::Checkmark>(
        tree.get_node("Camera/dialogue UI/menu ui/options_audio video/checkmark_colorblind/checkmark branch"));
    ASSERT_NE(mark, nullptr);
    mark->UpdateCheckmark(true);
    EXPECT_EQ(mark->ui->get_texture(), "res://misc/checkmark_active2.png");
    mark->UpdateCheckmark(false);
    EXPECT_EQ(mark->ui->get_texture(), "res://misc/checkmark_inactive.png");
}

}  // namespace
