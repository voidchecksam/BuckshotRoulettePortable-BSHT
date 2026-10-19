// The ACHIEVEMENTS screen (the one addition to the original's menu) and the
// unlock toast Steam's overlay would show. Hidden achievements are the ones
// Steam lists without a description (steamcommunity.com/stats/2835570/
// achievements): High Rollers, Name Taken, Soak It In, Why?, Going Out With
// Style!, Full House.
#include "app/achievements_screen.h"
#include "app/scene_host.h"
#include "assets/ui_layout.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"
#include "game/achievements.h"
#include "game/save_store.h"

#include <cstdio>
#include <string>

#include "test_data.h"

#include <gtest/gtest.h>

namespace {

const std::string kText = std::string(BSHT_SOURCE_DIR) + "/tools/achievements_en.json";

struct Screen : ::testing::Test {
    game::SaveStore store{std::string(BSHT_BINARY_DIR) + "/ach_screen_" +
                          ::testing::UnitTest::GetInstance()->current_test_info()->name()};
    game::Achievements achievements;
    void SetUp() override {
        store.Clear("achievements");
        ASSERT_TRUE(achievements.LoadText(kText.c_str()));
        achievements.Attach(&store);
    }
};

TEST_F(Screen, ListsAllSixteenLockedRowsGrey) {
    const app::AchievementsScreen screen(achievements);
    const auto rows = screen.Rows();
    ASSERT_EQ(rows.size(), 16u);
    EXPECT_EQ(rows[0].title, "70K");
    for (const auto& r : rows) EXPECT_LT(r.rgba[0], 1.0f) << r.title;
    achievements.Unlock("ach1");
    EXPECT_FLOAT_EQ(screen.Rows()[0].rgba[0], 1.0f);
}

TEST_F(Screen, AHiddenAchievementShowsQuestionMarksUntilUnlocked) {
    const app::AchievementsScreen screen(achievements);
    EXPECT_EQ(screen.Rows()[10].description, "???");  // ach11 Name Taken
    EXPECT_NE(screen.Rows()[0].description, "???");   // ach1 is not hidden
    achievements.Unlock("ach11");
    EXPECT_EQ(screen.Rows()[10].description, "Enter the name \"GOD\" when signing the waiver.");
}

TEST_F(Screen, ThePadScrollsAWindowAndCancelCloses) {
    app::AchievementsScreen screen(achievements);
    screen.Open();
    EXPECT_TRUE(screen.IsOpen());
    EXPECT_EQ(screen.FirstVisible(), 0);
    for (int i = 0; i < 20; ++i) screen.MoveDown();
    EXPECT_EQ(screen.FirstVisible(), 16 - app::AchievementsScreen::kVisibleRows);
    screen.MoveUp();
    EXPECT_EQ(screen.FirstVisible(), 16 - app::AchievementsScreen::kVisibleRows - 1);
    screen.Cancel();
    EXPECT_FALSE(screen.IsOpen());
}

TEST_F(Screen, TheToastShowsEachUnlockForFourSecondsInTurn) {
    app::AchievementToast toast;
    achievements.Unlock("ach8");
    achievements.Unlock("ach13");
    toast.Update(0.0f, achievements);
    ASSERT_TRUE(toast.Showing());
    EXPECT_EQ(toast.Current().name, "Coin Flip");
    toast.Update(3.9f, achievements);
    EXPECT_EQ(toast.Current().name, "Coin Flip");
    toast.Update(0.2f, achievements);
    ASSERT_TRUE(toast.Showing());
    EXPECT_EQ(toast.Current().name, "Why?");
    toast.Update(4.1f, achievements);
    EXPECT_FALSE(toast.Showing());
}

}  // namespace

// The entry and the open screen lay out separately, so the entry can draw
// under the menu's own controls (the splash's black covers it) and the open
// screen over them.
TEST_F(Screen, TheEntryAndTheOpenScreenLayOutSeparately) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_fonts.json");
    assets::FontAtlas fonts;
    ASSERT_EQ(fonts.Load((std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_fonts.json").c_str(), nullptr),
              assets::FontError::None);
    app::AchievementsScreen screen(achievements);
    app::OverlayDraw entry, open;
    screen.LayoutEntry(fonts, true, entry);
    screen.LayoutScreen(fonts, open);
    EXPECT_FALSE(entry.text.empty());
    EXPECT_TRUE(open.text.empty()) << "closed: no screen";
    screen.Open();
    app::OverlayDraw entryWhileOpen;
    screen.LayoutEntry(fonts, true, entryWhileOpen);
    screen.LayoutScreen(fonts, open);
    EXPECT_TRUE(entryWhileOpen.text.empty()) << "the open screen replaces the entry";
    EXPECT_FALSE(open.text.empty());
}

// ACHIEVEMENTS reads as the menu's next row: its text is laid out as the menu
// lays out its own buttons (centred in EXIT's label box), one CREDITS -> EXIT
// step below EXIT's.
TEST_F(Screen, TheEntryReadsOneMenuRowBelowExit) {
    const std::string data = std::string(BSHT_SOURCE_DIR) + "/assets/extracted";
    godot::SceneTree tree;
    if (godot::LoadRuntimeScene((data + "/runtime_menu.json").c_str(), tree) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "export runtime_menu.json first";
    }
    assets::UiLayout layout;
    ASSERT_EQ(layout.Load((data + "/ui_layout_menu.json").c_str(), nullptr), assets::UiLayoutError::None);
    assets::FontAtlas fonts;
    ASSERT_EQ(fonts.Load((data + "/ui_fonts.json").c_str(), nullptr), assets::FontError::None);
    const int ei = layout.Find("Camera/dialogue UI/menu ui/main screen/button_exit");
    const int ci = layout.Find("Camera/dialogue UI/menu ui/main screen/button_credits");
    ASSERT_GE(ei, 0);
    ASSERT_GE(ci, 0);
    const assets::UiControl& exit = layout.Controls()[static_cast<std::size_t>(ei)];
    const assets::UiControl& credits = layout.Controls()[static_cast<std::size_t>(ci)];

    // EXIT's text as GameApp::DrawTextRun draws it.
    std::vector<assets::TextQuad> q;
    fonts.Layout(fonts.Find(exit.fontKey), "EXIT", 0.0f, 0.0f, exit.w / exit.scale[0], exit.h / exit.scale[1],
                 assets::AlignH::Centre, static_cast<assets::AlignV>(exit.alignV), q, exit.lineSpacing);
    ASSERT_FALSE(q.empty());
    const float exitBaseline = exit.y + (q[0].y + q[0].h) * exit.scale[1];

    ASSERT_NE(app::AddAchievementsEntry(tree), nullptr);
    const app::ScreenRect box = app::AchievementsEntryTextBox(tree);
    app::AchievementsScreen screen(achievements);
    screen.SetEntryText(0.0f, box.y, box.h);
    app::OverlayDraw out;
    screen.LayoutEntry(fonts, true, out);
    ASSERT_FALSE(out.text.empty());
    ASSERT_FALSE(out.text[0].quads.empty());
    const assets::TextQuad& a = out.text[0].quads[0];   // 'A', a capital on the baseline like 'E'
    EXPECT_NEAR(a.y + a.h, exitBaseline + (exit.y - credits.y), 0.5f);
}

// The menu's buttons draw a focused or hovered entry at half grey (measured:
// 128/255) and nothing else; ACHIEVEMENTS dims the same way, with no
// rectangle.
TEST_F(Screen, ASelectedEntryDimsLikeTheMenusButtonsWithNoRectangle) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_fonts.json");
    assets::FontAtlas fonts;
    ASSERT_EQ(fonts.Load((std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_fonts.json").c_str(), nullptr),
              assets::FontError::None);
    app::AchievementsScreen screen(achievements);
    screen.SetEntrySelected(true);
    app::OverlayDraw entry;

    screen.LayoutEntry(fonts, true, entry);

    EXPECT_TRUE(entry.fills.empty());
    ASSERT_EQ(entry.text.size(), 1u);
    EXPECT_FLOAT_EQ(entry.text[0].rgba[0], 0.5f);
    EXPECT_FLOAT_EQ(entry.text[0].rgba[1], 0.5f);
    EXPECT_FLOAT_EQ(entry.text[0].rgba[2], 0.5f);
    EXPECT_FLOAT_EQ(entry.text[0].rgba[3], 1.0f);
}
