// The port's menu title and version: the blurry 3D title card is replaced by
// the port's own title (art/menu-title/port_menu_title.png, baked by
// tools/bake_menu_logo.py), and the version reads v1.0 (PSVITA).
#include "app/port_branding.h"
#include "app/scene_host.h"

#include "assets/font.h"
#include "assets/ui_layout.h"
#include "godot/control.h"
#include "godot/runtime_scene_loader.h"
#include "godot/translation.h"

#include <gtest/gtest.h>

#include <iterator>
#include <string>

namespace {

const std::string kData = std::string(BSHT_SOURCE_DIR) + "/assets/extracted";

// The menu tree with its title node shown, as MenuManager shows it.
bool LoadMenu(godot::SceneTree& tree) {
    if (godot::LoadRuntimeScene((kData + "/runtime_menu.json").c_str(), tree) != godot::RuntimeSceneError::None) {
        return false;
    }
    tree.get_node_as<godot::Node3D>(app::PortBranding::kTitleNode)->set_visible(true);
    return true;
}

// Apply sets the credit suffix process-wide: clear it after every test that
// applies the branding, or a later test reads the port's credit (found with
// --gtest_shuffle).
struct PortBrandingApplied : ::testing::Test {
    void TearDown() override { godot::set_message_suffix(app::PortBranding::kMainCreditsKey, ""); }
};

}  // namespace

// Apply runs before the scripts are ready and leaves the version alone:
// MenuManager._ready writes the game's, and WriteVersion goes over it after.
TEST_F(PortBrandingApplied, HidesTheTitleMeshAndLeavesTheVersionToAfterReady) {
    godot::SceneTree tree;
    if (godot::LoadRuntimeScene((kData + "/runtime_menu.json").c_str(), tree) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "export runtime_menu.json first";
    }
    auto* version = tree.get_node_as<godot::Label>(app::PortBranding::kVersionNode);
    ASSERT_NE(version, nullptr);
    const std::string before = version->get_text();
    ASSERT_TRUE(app::PortBranding::Apply(tree).ok);
    EXPECT_EQ(tree.get_node_as<godot::VisualInstance3D>("title")->get_layers(), 0u);
    EXPECT_EQ(version->get_text(), before);
    EXPECT_EQ(app::PortBranding::WriteVersion(tree), before);
    EXPECT_EQ(version->get_text(), "v1.0 (PSVITA)");
}

TEST(PortBranding, TheLogoTakesTheTitlesPlaceAtItsOwnPixelSize) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    assets::FontAtlas fonts;
    app::OverlayDraw out;
    app::PortBranding::Layout(tree, fonts, {276, 182}, out);
    ASSERT_EQ(out.logo.size(), 1u);
    EXPECT_TRUE(out.text.empty()) << "the logo replaces the text title";
    const app::FillRect& r = out.logo[0];
    // Drawn GL_NEAREST: 1:1 with its baked pixels, never resampled.
    EXPECT_FLOAT_EQ(r.w, 276.0f);
    EXPECT_FLOAT_EQ(r.h, 182.0f);
    EXPECT_NEAR(r.x + r.w * 0.5f, 480.0f, 0.5f);  // centred on the 960 design width
    EXPECT_FLOAT_EQ(r.x, 342.0f);                // a whole pixel: no half-texel seam
    // Where and how the original 3D title card sat (Godot menu frame 600: its
    // text 276 x 193 at (340, 96), grey 139): the logo reads as the original.
    EXPECT_GE(r.y, 96.0f);
    EXPECT_LE(r.y + r.h, 289.0f);
    EXPECT_NEAR(r.rgba[0], 139.0f / 255.0f, 0.01f);
    EXPECT_FLOAT_EQ(r.rgba[3], 1.0f);
}

TEST(PortBranding, TheLogoEndsAboveStart) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    assets::UiLayout layout;
    if (layout.Load((kData + "/ui_layout_menu.json").c_str(), nullptr) != assets::UiLayoutError::None) {
        GTEST_SKIP() << "export ui_layout_menu.json first";
    }
    const int start = layout.Find("Camera/dialogue UI/menu ui/main screen/true button_start");
    ASSERT_GE(start, 0);
    assets::FontAtlas fonts;
    app::OverlayDraw out;
    app::PortBranding::Layout(tree, fonts, {276, 182}, out);
    ASSERT_EQ(out.logo.size(), 1u);
    EXPECT_LT(out.logo[0].y + out.logo[0].h, layout.Controls()[static_cast<std::size_t>(start)].y);
    EXPECT_GE(out.logo[0].y, 0.0f);
}

TEST(PortBranding, ALogoTallerThanItsBandIsFittedIntoIt) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    assets::FontAtlas fonts;
    app::OverlayDraw out;
    app::PortBranding::Layout(tree, fonts, {1372, 905}, out);   // the PNG's own size
    ASSERT_EQ(out.logo.size(), 1u);
    const app::FillRect& r = out.logo[0];
    EXPECT_FLOAT_EQ(r.y, app::PortBranding::kLogoTop);
    EXPECT_FLOAT_EQ(r.y + r.h, app::PortBranding::kLogoBottom);
    EXPECT_NEAR(r.w / r.h, 1372.0f / 905.0f, 0.01f);   // the aspect survives
}

TEST(PortBranding, WithoutALogoFileTheTitleIsText) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    assets::FontAtlas fonts;
    ASSERT_EQ(fonts.Load((kData + "/ui_fonts.json").c_str(), nullptr), assets::FontError::None);
    app::OverlayDraw out;
    app::PortBranding::Layout(tree, fonts, {}, out);
    EXPECT_TRUE(out.logo.empty());
    ASSERT_EQ(out.text.size(), 2u);
    EXPECT_FALSE(out.text[0].quads.empty());
}

TEST(PortBranding, NothingDrawsWhileTheTitleNodeIsHidden) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    tree.get_node_as<godot::Node3D>(app::PortBranding::kTitleNode)->set_visible(false);
    assets::FontAtlas fonts;
    app::OverlayDraw out;
    app::PortBranding::Layout(tree, fonts, {276, 182}, out);
    EXPECT_TRUE(out.logo.empty());
    EXPECT_TRUE(out.text.empty());
}

TEST(PortBranding, AMissingNodeChangesNothing) {
    godot::SceneTree tree;
    const app::PortBranding::Applied done = app::PortBranding::Apply(tree);
    EXPECT_FALSE(done.ok);
    EXPECT_EQ(app::PortBranding::Describe(done), "menu: the port's title/version nodes are missing");
}

// The port's credit closes the main credits, just above the Critical Reflex
// logo. The menu's Discord corner is hidden (a link a Vita cannot open).
TEST_F(PortBrandingApplied, TheCreditClosesTheMainCredits) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    godot::set_translation_root(kData);
    ASSERT_TRUE(godot::set_locale("EN"));

    const app::PortBranding::Applied done = app::PortBranding::Apply(tree);
    ASSERT_TRUE(done.ok);

    const std::string credit = app::PortBranding::kPortCredit;
    EXPECT_NE(credit.find("Ported and rewritten in C++ by Abdul Muizz\n(justaverage_ / voidchecksum)"),
              std::string::npos);
    const std::string all = godot::tr("MAIN CREDITS");
    ASSERT_GT(all.size(), credit.size());
    EXPECT_EQ(all.substr(all.size() - credit.size()), credit);
    // The credit opens with its own blank line; the original's last line ends
    // right before it, with no newline of its own.
    const std::size_t at = all.size() - credit.size();
    EXPECT_EQ(credit.rfind("\n\n", 0), 0u);
    EXPECT_NE(all[at - 1], '\n') << "one blank line, not two, before the credit";
}

// The credit adds lines, so the logo and everything after it move down by
// exactly their height: Godot's gap above the logo stays as it is, in every
// locale. Nothing above the main credits moves.
TEST(PortBranding, MakingRoomMovesEverythingBelowTheMainCreditsDown) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    const char* kParent = "Camera/dialogue UI/menu ui/credits/credits move parent";
    const auto* main = tree.get_node_as<godot::Control>(app::PortBranding::kMainCreditsNode);
    const auto* logo = tree.get_node_as<godot::Control>(std::string(kParent) + "/critical reflex_logo");
    const auto* last = tree.get_node_as<godot::Control>(std::string(kParent) + "/credits_sound attribution");
    ASSERT_NE(main, nullptr);
    ASSERT_NE(logo, nullptr);
    ASSERT_NE(last, nullptr);
    const float mainY = main->get_position().y;
    const float logoY = logo->get_position().y;
    const float lastY = last->get_position().y;

    const int moved = app::PortBranding::MakeRoomForTheCredit(tree, 53.0f);

    EXPECT_EQ(moved, 13);   // the separators, the logos and every list after the main credits
    EXPECT_FLOAT_EQ(main->get_position().y, mainY);
    EXPECT_FLOAT_EQ(logo->get_position().y, logoY + 53.0f);
    EXPECT_FLOAT_EQ(last->get_position().y, lastY + 53.0f);
    EXPECT_FLOAT_EQ(logo->get_offset_since_load().y, 53.0f) << "the 2D draw reads this";
}

TEST(PortBranding, MakingNoRoomMovesNothing) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    EXPECT_EQ(app::PortBranding::MakeRoomForTheCredit(tree, 0.0f), 0);
    godot::SceneTree empty;
    EXPECT_EQ(app::PortBranding::MakeRoomForTheCredit(empty, 53.0f), 0);
}

// The credit's three lines (a blank one, the credit, the handle) are Latin, so
// each is as tall as the label's own font plus its line spacing, at its scale.
TEST(PortBranding, TheCreditIsThreeLinesOfTheLabelsOwnFont) {
    assets::UiLayout layout;
    if (layout.Load((kData + "/ui_layout_menu.json").c_str(), nullptr) != assets::UiLayoutError::None) {
        GTEST_SKIP() << "export ui_layout_menu.json first";
    }
    assets::FontAtlas fonts;
    ASSERT_EQ(fonts.Load((kData + "/ui_fonts.json").c_str(), nullptr), assets::FontError::None);
    const int ci = layout.Find(app::PortBranding::kMainCreditsNode);
    ASSERT_GE(ci, 0);
    const assets::UiControl& c = layout.Controls()[static_cast<std::size_t>(ci)];
    const int face = fonts.Find(c.fontKey);
    ASSERT_GE(face, 0);
    const assets::FontFace::LineMetric own = fonts.Faces()[static_cast<std::size_t>(face)].lineMetrics[0];

    const float expected = 3.0f * (own.ascent + own.descent + static_cast<float>(c.lineSpacing)) * c.scale[1];
    EXPECT_NEAR(app::PortBranding::CreditHeight(fonts, layout), expected, 1e-3f);
    EXPECT_GT(expected, 50.0f);
    EXPECT_LT(expected, 56.0f);
}

TEST(PortBranding, TheCreditHasNoHeightWithoutItsLabel) {
    assets::UiLayout layout;
    assets::FontAtlas fonts;
    EXPECT_FLOAT_EQ(app::PortBranding::CreditHeight(fonts, layout), 0.0f);
}

TEST_F(PortBrandingApplied, TheDiscordCornerIsGone) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";

    const app::PortBranding::Applied done = app::PortBranding::Apply(tree);
    ASSERT_TRUE(done.ok);

    EXPECT_EQ(done.hidden, static_cast<int>(std::size(app::PortBranding::kHiddenMenuNodes)));

    for (const char* path : app::PortBranding::kHiddenMenuNodes) {
        const auto* c = tree.get_node_as<godot::Control>(path);
        ASSERT_NE(c, nullptr) << path;
        EXPECT_FALSE(c->is_visible()) << path;
    }
}

// Debuggable (project rule: every subsystem logs what it did): a re-export
// that renames a node would otherwise bring the Discord corner back silently.
TEST_F(PortBrandingApplied, ItSaysWhatItDid) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";

    const std::string line = app::PortBranding::Describe(app::PortBranding::Apply(tree));

    EXPECT_EQ(line, "menu: port branding applied: credit after the main credits, 3/3 Discord nodes hidden");
}

namespace {

// v2.2's MULTIPLAYER entry stays, between START and OPTIONS (it says COMING
// SOON, MenuManager::ComingSoon), with the scene's own focus links.
TEST_F(PortBrandingApplied, TheMultiplayerEntryIsShownBetweenStartAndOptions) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    ASSERT_TRUE(app::PortBranding::Apply(tree).ok);
    const std::string screen = "Camera/dialogue UI/menu ui/main screen/";
    auto* button = tree.get_node_as<godot::Control>(screen + "true button_multiplayer");
    auto* label = tree.get_node_as<godot::Control>(screen + "button_multiplayer");
    ASSERT_NE(button, nullptr);
    ASSERT_NE(label, nullptr);
    EXPECT_TRUE(button->is_visible());
    EXPECT_TRUE(label->is_visible());
    auto* start = tree.get_node_as<godot::Control>(screen + "true button_start");
    ASSERT_NE(start, nullptr);
    godot::Control* down = start->_get_focus_neighbor(godot::SIDE_BOTTOM);
    ASSERT_NE(down, nullptr);
    EXPECT_EQ(down->get_name(), "true button_multiplayer");
    godot::Control* next = down->_get_focus_neighbor(godot::SIDE_BOTTOM);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->get_name(), "true button_options");
    godot::Control* up = down->_get_focus_neighbor(godot::SIDE_TOP);
    ASSERT_NE(up, nullptr);
    EXPECT_EQ(up->get_name(), "true button_start");
}

}  // namespace

namespace {

// The pad walks the whole main screen both ways, the port's
// ACHIEVEMENTS entry included (EXIT -> ACHIEVEMENTS -> START, and back up).
TEST_F(PortBrandingApplied, PadFocusWalksTheWholeMenuBothWays) {
    godot::SceneTree tree;
    if (!LoadMenu(tree)) GTEST_SKIP() << "export runtime_menu.json first";
    ASSERT_TRUE(app::PortBranding::Apply(tree).ok);
    ASSERT_NE(app::AddAchievementsEntry(tree), nullptr);
    const std::string screen = "Camera/dialogue UI/menu ui/main screen/";
    auto* start = tree.get_node_as<godot::Control>(screen + "true button_start");
    ASSERT_NE(start, nullptr);
    const std::vector<std::string> down = {"true button_multiplayer", "true button_options", "true button_credits",
                                           "true button_exit", "true button_achievements", "true button_start"};
    godot::Control* at = start;
    for (const std::string& want : down) {
        at = at ? at->_get_focus_neighbor(godot::SIDE_BOTTOM) : nullptr;
        ASSERT_NE(at, nullptr) << "down to " << want;
        EXPECT_EQ(at->get_name(), want);
    }
    at = start;
    for (auto it = down.rbegin() + 1; it != down.rend(); ++it) {
        at = at ? at->_get_focus_neighbor(godot::SIDE_TOP) : nullptr;
        ASSERT_NE(at, nullptr) << "up to " << *it;
        EXPECT_EQ(at->get_name(), *it);
    }
    at = at ? at->_get_focus_neighbor(godot::SIDE_TOP) : nullptr;
    ASSERT_NE(at, nullptr);
    EXPECT_EQ(at->get_name(), "true button_start");
}

}  // namespace
