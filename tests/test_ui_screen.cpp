// One open UI screen: membership and what to draw, decided by the scene tree.
//
// Focus and navigation belong to Godot's Viewport (godot/gui.cpp). What stays
// here is drawing: a control is drawn only while its node is visible in the
// tree, with the node's modulate, because the translated scripts
// (ButtonClass_Main.SetUI, DescriptionManager, DialogueManager) write those.
//
// Run against the real exports: ui_layout.json for rects and fonts,
// runtime_main.json for the live tree.
#include "game/ui_screen.h"

#include "godot/control.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <cmath>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace {

const char* kItemGrabbing = "Camera/dialogue UI/bracket ui/item grabbing";
const char* kShooting = "Camera/dialogue UI/shooting selection";
const char* kShootBrackets = "Camera/dialogue UI/bracket ui/shooting choice";
const char* kDealerBracket = "Camera/dialogue UI/bracket ui/shooting choice/m button_choice dealer/TextureRect";

std::string SourcePath(const char* rel) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + rel;
}

bool Exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

#define REQUIRE_EXPORT()                                                                      \
    if (!Exists(SourcePath("ui_layout.json")) || !Exists(SourcePath("runtime_main.json")))    \
    GTEST_SKIP() << "run tools/godot/export_ui.gd and export_runtime_scene.gd first"

struct Scene {
    assets::UiLayout layout;
    godot::SceneTree tree;
    Scene() {
        EXPECT_STREQ("ok", assets::UiLayoutErrorText(layout.Load(SourcePath("ui_layout.json").c_str(), nullptr)));
        EXPECT_EQ(godot::LoadRuntimeScene(SourcePath("runtime_main.json").c_str(), tree),
                  godot::RuntimeSceneError::None);
    }
    godot::Control* Control(const char* path) { return tree.get_node_as<godot::Control>(path); }
};

}  // namespace

// --- membership ------------------------------------------------------------------

TEST(UiScreen, OpensTheItemGrabbingScreen) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kItemGrabbing));
    EXPECT_TRUE(screen.IsOpen());
    EXPECT_FALSE(screen.Members().empty());
    EXPECT_EQ(screen.Unbound(), 0) << "every layout control has a node in the tree";
}

TEST(UiScreen, OpeningAnUnknownPrefixFails) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    EXPECT_FALSE(screen.Open("Camera/dialogue UI/no such screen"));
    EXPECT_FALSE(screen.IsOpen());
}

TEST(UiScreen, ClosingClearsEverything) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kItemGrabbing));
    screen.Close();
    EXPECT_FALSE(screen.IsOpen());
    EXPECT_TRUE(screen.Members().empty());
    std::vector<game::UiDrawItem> items;
    screen.BuildDrawList(items);
    EXPECT_TRUE(items.empty());
}

// The scene's own facts about the item grid, independent of who owns focus:
// the player's four slots sit left, the dealer's four right, briefcase between.
TEST(UiScreen, TheItemGridIsTwoClustersNotOneRow) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    ASSERT_TRUE(screen.Open(kItemGrabbing));
    int left = 0;
    int right = 0;
    int lower = 0;
    for (int i : screen.Members()) {
        const assets::UiControl& c = s.layout.Controls()[static_cast<std::size_t>(i)];
        if (!c.isButton || c.name.find("grid place item") == std::string::npos) continue;
        if (c.CentreX() < 480.0f) ++left; else ++right;
        if (c.CentreY() > 200.0f) ++lower;
    }
    EXPECT_EQ(4, left);
    EXPECT_EQ(4, right);
    EXPECT_EQ(4, lower);
}

// 6 of the 68 buttons drive GeometryInstance3D.transparency instead of a
// CanvasItem alpha; the briefcase intake on this screen is one of them.
TEST(UiScreen, TheBriefcaseIntakeHighlightsGeometryNotABracket) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    ASSERT_TRUE(screen.Open(kItemGrabbing));
    int geometry = 0;
    for (int i : screen.Members()) {
        const assets::UiControl& c = s.layout.Controls()[static_cast<std::size_t>(i)];
        if (!c.isButton || !c.is3d) continue;
        ++geometry;
        EXPECT_EQ(-1, c.highlight);
        EXPECT_FALSE(c.highlight3d.empty());
    }
    EXPECT_GE(geometry, 1);
}

// The first hardware build opened only the text half: brackets and text are
// two subtrees.
TEST(UiScreen, TheShootingInterfaceIsTwoSubtrees) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen brackets(s.layout);
    game::UiScreen text(s.layout);
    ASSERT_TRUE(brackets.Open(kShootBrackets));
    ASSERT_TRUE(text.Open(kShooting));
    const auto hasButton = [&](const game::UiScreen& sc) {
        return std::any_of(sc.Members().begin(), sc.Members().end(), [&](int i) {
            return s.layout.Controls()[static_cast<std::size_t>(i)].isButton;
        });
    };
    EXPECT_TRUE(hasButton(brackets));
    EXPECT_FALSE(hasButton(text));
}

TEST(UiScreen, MembersAreDeduplicatedAcrossOverlappingPrefixes) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen once(s.layout);
    game::UiScreen twice(s.layout);
    ASSERT_TRUE(once.Open(kItemGrabbing));
    ASSERT_TRUE(twice.OpenAll({kItemGrabbing, kItemGrabbing, "Camera/dialogue UI/bracket ui"}));
    std::vector<int> seen;
    for (int i : twice.Members()) {
        EXPECT_EQ(std::find(seen.begin(), seen.end(), i), seen.end()) << "control " << i << " listed twice";
        seen.push_back(i);
    }
    EXPECT_GE(twice.Members().size(), once.Members().size());
}

TEST(UiScreen, MembersStayInSceneOrderNotPrefixOrder) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen a(s.layout);
    game::UiScreen b(s.layout);
    ASSERT_TRUE(a.OpenAll({kShootBrackets, kShooting}));
    ASSERT_TRUE(b.OpenAll({kShooting, kShootBrackets}));
    EXPECT_EQ(a.Members(), b.Members());
    for (std::size_t i = 1; i < a.Members().size(); ++i) EXPECT_LT(a.Members()[i - 1], a.Members()[i]);
}

// --- drawing follows the tree ----------------------------------------------------

// One rule: without a tree nothing is known about what is on screen, so nothing
// is drawn (never the export's load-time guess).
TEST(UiScreen, AScreenWithNoTreeDrawsNothing) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    ASSERT_TRUE(screen.Open(kShooting));
    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    EXPECT_TRUE(runs.empty());
    std::vector<game::UiDrawItem> items;
    screen.BuildDrawList(items);
    EXPECT_TRUE(items.empty());
}

// `dialogue UI/shooting selection` is hidden at load in the original, and no
// script or animation ever shows it (the choice is the 3D Label3D text), so
// its Labels ("DEALER / SELF") must never be drawn while it is hidden.
TEST(UiScreen, TheShootingSelectionTextIsNotDrawnWhileItsParentIsHidden) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.OpenAll({kShootBrackets, kShooting}));
    ASSERT_NE(s.Control(kShooting), nullptr);
    ASSERT_FALSE(s.Control(kShooting)->is_visible());
    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    EXPECT_TRUE(runs.empty());
}

// Each Label lays out with its own line_spacing: the default theme's 3, or 0
// on the dialogue box and the item descriptions (theme overrides).
TEST(UiScreen, EveryRunCarriesItsLabelsLineSpacing) {
    REQUIRE_EXPORT();
    Scene s;
    const char* kDialogue = "Camera/dialogue UI/main dialogue";
    const char* kDialogueText = "Camera/dialogue UI/main dialogue/dialogue text";
    const char* kEnding = "Camera/dialogue UI/ending ui";
    const char* kCash = "Camera/dialogue UI/ending ui/cash";
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.OpenAll({kDialogue, kEnding}));
    for (const char* path : {kDialogue, kDialogueText, kEnding, kCash}) {
        ASSERT_NE(s.Control(path), nullptr) << path;
        s.Control(path)->set_visible(true);
    }
    godot::cast_to<godot::Label>(s.Control(kDialogueText))->set_text("ONE\nTWO");
    godot::cast_to<godot::Label>(s.Control(kCash))->set_text("CASH");
    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    int seen = 0;
    for (const game::UiTextRun& r : runs) {
        if (r.control == s.layout.Find(kDialogueText)) {
            EXPECT_EQ(r.lineSpacing, 0);
            ++seen;
        } else if (r.control == s.layout.Find(kCash)) {
            EXPECT_EQ(r.lineSpacing, 3);
            ++seen;
        }
    }
    EXPECT_EQ(seen, 2);
}

// v2.2's ending stat lines trim their 64 dots at the label's width
// (text_overrun_behavior TRIM_CHAR); other labels never trim.
TEST(UiScreen, OnlyTheTrimmingLabelsMarkTheirRunsForTrimming) {
    REQUIRE_EXPORT();
    Scene s;
    const char* kEnding = "Camera/dialogue UI/ending ui";
    const char* kShots = "Camera/dialogue UI/ending ui/shots fired";
    const char* kCash = "Camera/dialogue UI/ending ui/cash";
    const int shots = s.layout.Find(kShots);
    ASSERT_GE(shots, 0);
    EXPECT_EQ(s.layout.Controls()[static_cast<std::size_t>(shots)].overrun, 1);
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kEnding));
    s.Control(kEnding)->set_visible(true);
    for (const char* path : {kShots, kCash}) {
        auto* label = godot::cast_to<godot::Label>(s.Control(path));
        ASSERT_NE(label, nullptr) << path;
        label->set_visible(true);
        label->set_text("SHOTS FIRED " + std::string(64, '.'));
    }
    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    int seen = 0;
    for (const game::UiTextRun& r : runs) {
        if (r.control == shots) {
            EXPECT_TRUE(r.trimChars);
            ++seen;
        } else if (r.control == s.layout.Find(kCash)) {
            EXPECT_FALSE(r.trimChars);
            ++seen;
        }
    }
    EXPECT_EQ(seen, 2);
}

TEST(UiScreen, TextIsDrawnWhenTheTreeShowsItWithShadowAndForeground) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kShooting));
    s.Control(kShooting)->set_visible(true);

    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    ASSERT_FALSE(runs.empty());
    int dark = 0;
    int light = 0;
    for (const game::UiTextRun& r : runs) {
        EXPECT_FALSE(r.fontKey.empty());
        EXPECT_FALSE(r.text.empty());
        EXPECT_GT(r.rgba[3], 0.0f);
        EXPECT_GT(r.w, 0.0f);
        if (r.rgba[0] + r.rgba[1] + r.rgba[2] < 0.5f) ++dark; else ++light;
    }
    EXPECT_GT(dark, 0) << "no shadow labels";
    EXPECT_GT(light, 0) << "no foreground labels";
}

// DescriptionManager fades its Labels by writing modulate.a.
TEST(UiScreen, TextAlphaIsTheNodesModulate) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kShooting));
    s.Control(kShooting)->set_visible(true);

    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    ASSERT_FALSE(runs.empty());
    const int target = runs.front().control;
    const float before = runs.front().rgba[3];
    godot::Control* node = s.Control(s.layout.Controls()[static_cast<std::size_t>(target)].path.c_str());
    ASSERT_NE(node, nullptr);
    godot::Color m = node->get_modulate();
    m.a *= 0.5f;
    node->set_modulate(m);
    screen.BuildTextList(runs);
    ASSERT_FALSE(runs.empty());
    EXPECT_EQ(runs.front().control, target);
    EXPECT_NEAR(runs.front().rgba[3], before * 0.5f, 1e-5f);
}

// ButtonClass_Main.SetUI writes the bracket's modulate.a (0 hidden, 1 shown).
TEST(UiScreen, BracketAlphaIsTheNodesModulateAndItsTreeVisibility) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kShootBrackets));
    godot::Control* bracket = s.Control(kDealerBracket);
    ASSERT_NE(bracket, nullptr);
    const int index = s.layout.Find(kDealerBracket);
    ASSERT_GE(index, 0);

    const auto drawn = [&] {
        std::vector<game::UiDrawItem> items;
        screen.BuildDrawList(items);
        for (const game::UiDrawItem& it : items) {
            if (it.control == index) return it.rgba[3];
        }
        return -1.0f;
    };

    EXPECT_LT(drawn(), 0.0f) << "parent hidden and alpha 0 at load";
    godot::Color m = bracket->get_modulate();
    m.a = 1.0f;
    bracket->set_modulate(m);
    EXPECT_LT(drawn(), 0.0f) << "still hidden by `shooting choice`";
    s.Control(kShootBrackets)->set_visible(true);
    EXPECT_GT(drawn(), 0.0f);
    m.a = 0.0f;
    bracket->set_modulate(m);
    EXPECT_LT(drawn(), 0.0f) << "a fully transparent quad is never queued";
}

// PickupIndicator.BeginPickup writes the item's name into the description
// Labels; what is drawn is the node's text, not the export's placeholder
// ("Shotgun" / "CHOOSE BETWEEN SHOOTING...").
TEST(UiScreen, TextIsTheTreeLabelsTextNotTheExportPlaceholder) {
    REQUIRE_EXPORT();
    const char* kDescription = "Camera/dialogue UI/item description";
    const char* kItemName = "Camera/dialogue UI/item description/item name";
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kDescription));
    s.Control(kDescription)->set_visible(true);
    auto* label = s.tree.get_node_as<godot::Label>(kItemName);
    ASSERT_NE(label, nullptr);
    label->set_visible(true);
    godot::Color m = label->get_modulate();
    m.a = 1.0f;
    label->set_modulate(m);
    label->set_text("SHOTGUN");

    const int index = s.layout.Find(kItemName);
    ASSERT_GE(index, 0);
    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    bool found = false;
    for (const game::UiTextRun& r : runs) {
        if (r.control != index) continue;
        found = true;
        EXPECT_EQ("SHOTGUN", r.text);
    }
    EXPECT_TRUE(found) << "the item name label is drawn";
}

// TextShaker writes `position` on "main dialogue" every 0.03 s; the layout's
// rect is the load-time one, so the draw must follow the node's move.
TEST(UiScreen, TextFollowsItsControlsMovedPosition) {
    REQUIRE_EXPORT();
    const char* kDialogue = "Camera/dialogue UI/main dialogue";
    const char* kDialogueText = "Camera/dialogue UI/main dialogue/dialogue text";
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kDialogue));
    godot::Control* box = s.Control(kDialogue);
    ASSERT_NE(box, nullptr);
    box->set_visible(true);
    auto* label = s.tree.get_node_as<godot::Label>(kDialogueText);
    ASSERT_NE(label, nullptr);
    label->set_visible(true);
    label->set_text("1 LIVE ROUND. 2 BLANKS.");
    godot::Color m = label->get_modulate();
    m.a = 1.0f;
    label->set_modulate(m);

    const int index = s.layout.Find(kDialogueText);
    ASSERT_GE(index, 0);
    const auto runX = [&] {
        std::vector<game::UiTextRun> runs;
        screen.BuildTextList(runs);
        for (const game::UiTextRun& r : runs) {
            if (r.control == index) return r.x;
        }
        return -1.0f;
    };
    const float before = runX();
    ASSERT_GE(before, 0.0f) << "the dialogue text is drawn";

    box->set_position({box->get_position().x + 3.0f, box->get_position().y + 4.0f});

    EXPECT_FLOAT_EQ(runX(), before + 3.0f);
}

// --- runtime overrides (the dialogue box) ------------------------------------------

TEST(UiScreen, TextCanBeOverriddenAtRuntime) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kShooting));
    s.Control(kShooting)->set_visible(true);
    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    ASSERT_FALSE(runs.empty());
    const int target = runs.front().control;
    ASSERT_TRUE(screen.SetText(target, "2 LIVE, 3 BLANK"));
    screen.BuildTextList(runs);
    bool found = false;
    for (const game::UiTextRun& r : runs) {
        if (r.control == target) {
            EXPECT_EQ("2 LIVE, 3 BLANK", r.text);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(UiScreen, OverridingTextOutsideTheScreenIsRejected) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    ASSERT_TRUE(screen.Open(kShooting));
    EXPECT_FALSE(screen.SetText(-1, "nope"));
    EXPECT_FALSE(screen.SetText(99999, "nope"));
}

TEST(UiScreen, ReopeningClearsStaleTextOverrides) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kShooting));
    s.Control(kShooting)->set_visible(true);
    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    ASSERT_FALSE(runs.empty());
    const int target = runs.front().control;
    const std::string original = runs.front().text;
    ASSERT_TRUE(screen.SetText(target, "STALE"));
    ASSERT_TRUE(screen.Open(kShooting));
    screen.BuildTextList(runs);
    for (const game::UiTextRun& r : runs) {
        if (r.control == target) EXPECT_EQ(original, r.text);
    }
}

TEST(UiScreen, AnEmptyOverrideDrawsNothingRatherThanTheOriginal) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kShooting));
    s.Control(kShooting)->set_visible(true);
    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    const std::size_t before = runs.size();
    ASSERT_GT(before, 0u);
    ASSERT_TRUE(screen.SetText(runs.front().control, ""));
    screen.BuildTextList(runs);
    EXPECT_EQ(before - 1, runs.size());
}

TEST(UiScreen, TextListIsEmptyOnAScreenWithNoLabels) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kItemGrabbing));
    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    EXPECT_TRUE(runs.empty());
}

// Shooting yourself with a live shell blacks the screen out:
// DeathManager.Kill sets viewblocker.visible = true. A ColorRect has no
// texture, so its colour must be exported (export_ui.gd), and the subtree is
// part of the table screen.
//
// The scene also starts with the blackout on: the game begins inside the
// restroom intro, and IntroManager hides it when the intro ends. A diagnostic
// boot skips the intro, so Session::Start hides it; without that the whole
// game is black.
TEST(UiScreen, TheDeathBlackoutIsAFullScreenBlackFill) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.OpenAll({"Camera/dialogue UI/viewblocker parent"}));

    godot::Control* blocker = s.Control("Camera/dialogue UI/viewblocker parent/viewblcoker");
    ASSERT_NE(blocker, nullptr);

    const auto fullScreenFills = [&screen]() {
        std::vector<game::UiDrawItem> items;
        screen.BuildDrawList(items);
        std::vector<game::UiDrawItem> big;
        for (const game::UiDrawItem& it : items) {
            if (it.w >= 900.0f && it.h >= 500.0f) big.push_back(it);
        }
        return big;
    };

    blocker->set_visible(true);
    const std::vector<game::UiDrawItem> shown = fullScreenFills();
    ASSERT_FALSE(shown.empty()) << "no full-screen quad: the ColorRect colour is missing again";
    EXPECT_FLOAT_EQ(shown[0].rgba[0], 0.0f);
    EXPECT_FLOAT_EQ(shown[0].rgba[1], 0.0f);
    EXPECT_FLOAT_EQ(shown[0].rgba[2], 0.0f);
    EXPECT_FLOAT_EQ(shown[0].rgba[3], 1.0f) << "opaque, or it is not a blackout";

    // ...and hidden it must not cover the game. Session::Start hides it at
    // startup because the port skips the intro that would have.
    blocker->set_visible(false);
    EXPECT_TRUE(fullScreenFills().empty()) << "the blackout is drawn while hidden";
}

// A ColorRect fills with its live colour. The menu's splash fades
// viewblocker.color to transparent; drawn with the load-time colour, the menu
// would stay black behind its buttons.
TEST(UiScreen, AColorRectDrawsItsLiveColour) {
    REQUIRE_EXPORT();
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.OpenAll({"Camera/dialogue UI/viewblocker parent"}));
    auto* blocker = godot::cast_to<godot::ColorRect>(
        s.Control("Camera/dialogue UI/viewblocker parent/viewblcoker"));
    ASSERT_NE(blocker, nullptr);
    blocker->set_visible(true);

    blocker->set_color({0.0f, 0.0f, 0.0f, 0.5f});
    std::vector<game::UiDrawItem> items;
    screen.BuildDrawList(items);
    bool half = false;
    for (const auto& it : items) half = half || (it.w >= 900.0f && std::fabs(it.rgba[3] - 0.5f) < 1e-4f);
    EXPECT_TRUE(half);

    blocker->set_color({0.0f, 0.0f, 0.0f, 0.0f});
    screen.BuildDrawList(items);
    for (const auto& it : items) EXPECT_LT(it.w, 900.0f) << "a transparent ColorRect is still drawn";
}

// The hold-to-exit hint fades by animating self_modulate; it loads at alpha 0
// (the scene's value) and draws once something raises it.
TEST(UiScreen, TextAlphaIncludesTheNodesLiveSelfModulate) {
    REQUIRE_EXPORT();
    Scene s;
    const char* kHint = "Camera/dialogue UI/hold to exit label";
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open(kHint));
    godot::Control* hint = s.Control(kHint);
    ASSERT_NE(hint, nullptr);
    EXPECT_NEAR(hint->get_self_modulate().a, 0.0f, 1e-6f) << "loaded from the runtime export";

    std::vector<game::UiTextRun> runs;
    screen.BuildTextList(runs);
    EXPECT_TRUE(runs.empty()) << "self_modulate alpha 0 draws nothing";

    hint->set_self_modulate({1.0f, 1.0f, 1.0f, 0.5f});
    screen.BuildTextList(runs);
    ASSERT_FALSE(runs.empty());
    EXPECT_NEAR(runs.back().rgba[3], 0.5f, 1e-5f);
}

// Fills and text interleave in tree order; at one control the fill draws
// first.
TEST(UiDrawSequence, FillsAndTextKeepSceneOrder) {
    std::vector<game::UiDrawItem> items(3);
    items[0].control = 1; items[1].control = 4; items[2].control = 6;
    std::vector<game::UiTextRun> runs(2);
    runs[0].control = 4; runs[1].control = 5;
    std::vector<game::UiDrawStep> seq;
    game::UiDrawSequence(items, runs, seq);
    ASSERT_EQ(seq.size(), 5u);
    const bool text[5] = {false, false, true, true, false};
    for (int k = 0; k < 5; ++k) EXPECT_EQ(seq[static_cast<std::size_t>(k)].text, text[k]) << k;
}

// A scene change out of main loads a smaller layout into the same UiLayout.
// The old screen's member indices must not be resolved against it
// (vector::operator[] out of range, an abort): a member the layout no longer
// has is unbound, never indexed.
TEST(UiScreen, RebindingAfterTheLayoutShrankIndexesNothingPastItsEnd) {
    REQUIRE_EXPORT();
    if (!Exists(SourcePath("ui_layout_heaven.json")) || !Exists(SourcePath("runtime_heaven.json"))) {
        GTEST_SKIP() << "heaven exports missing";
    }
    Scene s;
    game::UiScreen screen(s.layout);
    screen.BindTree(&s.tree);
    ASSERT_TRUE(screen.Open("Camera/dialogue UI"));
    const int highest = *std::max_element(screen.Members().begin(), screen.Members().end());

    ASSERT_STREQ("ok", assets::UiLayoutErrorText(s.layout.Load(SourcePath("ui_layout_heaven.json").c_str(), nullptr)));
    ASSERT_LT(static_cast<std::size_t>(highest), static_cast<std::size_t>(highest + 1));
    ASSERT_GT(static_cast<std::size_t>(highest), s.layout.Controls().size()) << "the test needs a shrunk layout";
    godot::SceneTree heaven;
    ASSERT_EQ(godot::LoadRuntimeScene(SourcePath("runtime_heaven.json").c_str(), heaven), godot::RuntimeSceneError::None);

    screen.BindTree(&heaven);   // must not index the old members into the new layout
    EXPECT_GT(screen.Unbound(), 0);
}

// v2.2: the options' checkmarks (ChecmarkBranch.UpdateCheckmark) swap a
// TextureRect's texture; the screen draws the one the script set.
TEST(UiScreen, ATextureRectDrawsTheTextureAScriptSet) {
    REQUIRE_EXPORT();
    assets::UiLayout layout;
    ASSERT_STREQ("ok", assets::UiLayoutErrorText(layout.Load(SourcePath("ui_layout_menu.json").c_str(), nullptr)));
    godot::SceneTree tree;
    ASSERT_EQ(godot::LoadRuntimeScene(SourcePath("runtime_menu.json").c_str(), tree), godot::RuntimeSceneError::None);
    const std::string kScreen = "Camera/dialogue UI/menu ui/options_audio video";
    const std::string kBox = kScreen + "/checkmark_colorblind";
    game::UiScreen screen(layout);
    screen.BindTree(&tree);
    ASSERT_TRUE(screen.OpenAll({kScreen}));
    tree.get_node_as<godot::Control>(kScreen)->set_visible(true);
    auto* box = tree.get_node_as<godot::TextureRect>(kBox);
    ASSERT_NE(box, nullptr);
    const int active = layout.FindTexture("res://misc/checkmark_active2.png");
    ASSERT_GE(active, 0);

    box->set_texture("res://misc/checkmark_active2.png");
    std::vector<game::UiDrawItem> items;
    screen.BuildDrawList(items);
    int drawn = -2;
    for (const auto& it : items) {
        if (it.control == layout.Find(kBox)) drawn = it.texture;
    }
    EXPECT_EQ(drawn, active);
}

// A texture the layout never exported keeps the control's own, and says so once.
TEST(UiScreen, ATextureTheLayoutLacksIsLoggedOnce) {
    REQUIRE_EXPORT();
    assets::UiLayout layout;
    ASSERT_STREQ("ok", assets::UiLayoutErrorText(layout.Load(SourcePath("ui_layout_menu.json").c_str(), nullptr)));
    godot::SceneTree tree;
    ASSERT_EQ(godot::LoadRuntimeScene(SourcePath("runtime_menu.json").c_str(), tree), godot::RuntimeSceneError::None);
    const std::string kScreen = "Camera/dialogue UI/menu ui/options_audio video";
    game::UiScreen screen(layout);
    std::vector<std::string> logged;
    screen.SetLog([&logged](const std::string& line) { logged.push_back(line); });
    screen.BindTree(&tree);
    ASSERT_TRUE(screen.OpenAll({kScreen}));
    tree.get_node_as<godot::Control>(kScreen)->set_visible(true);
    tree.get_node_as<godot::TextureRect>(kScreen + "/checkmark_colorblind")->set_texture("res://misc/nowhere.png");
    std::vector<game::UiDrawItem> items;
    screen.BuildDrawList(items);
    screen.BuildDrawList(items);
    ASSERT_EQ(logged.size(), 1u);
    EXPECT_NE(logged[0].find("res://misc/nowhere.png"), std::string::npos) << logged[0];

    // A tree bound afterwards is the next scene: it reports its own.
    screen.BindTree(&tree);
    ASSERT_TRUE(screen.OpenAll({kScreen}));
    screen.BuildDrawList(items);
    EXPECT_EQ(logged.size(), 2u);
}
