// The 2D UI layout, its texture pool and the gamepad focus graph.
//
// These run against the real exported ui_layout.json rather than a fixture.
// The numbers below (175 controls, 185 explicit links, one bracket texture
// shared 61 ways) are properties of the actual game, and a fixture would go on
// passing after a re-export changed them.
#include "assets/ui_layout.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <map>
#include <string>

namespace {

std::string LayoutPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_layout.json";
}
std::string PixelsPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_textures.bin";
}

bool Exported() {
    std::FILE* f = std::fopen(LayoutPath().c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// Returns an empty layout when the export is absent; every test guards on
// Exported() first so a missing export skips rather than fails mysteriously.
assets::UiLayout Load(bool withPixels = true) {
    assets::UiLayout ui;
    const std::string pix = PixelsPath();
    const assets::UiLayoutError e =
        ui.Load(LayoutPath().c_str(), withPixels ? pix.c_str() : nullptr);
    EXPECT_STREQ("ok", assets::UiLayoutErrorText(e));
    return ui;
}

#define REQUIRE_EXPORT()     if (!Exported()) GTEST_SKIP() << "run tools/godot/export_ui.gd first"

TEST(UiLayout, LoadsEveryControlFromTheScene) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load();
    EXPECT_EQ(180, static_cast<int>(ui.Controls().size()));
    EXPECT_EQ(960, ui.DesignWidth());
    EXPECT_EQ(540, ui.DesignHeight());
}

TEST(UiLayout, EveryControlHasAMeasuredRect) {
    REQUIRE_EXPORT();
    // A zero-area rect draws nothing and reads as a missing feature rather
    // than a broken export, so it must fail here instead.
    const assets::UiLayout ui = Load(false);
    int degenerate = 0;
    for (const assets::UiControl& c : ui.Controls()) {
        if (c.w <= 0.0f || c.h <= 0.0f) ++degenerate;
    }
    EXPECT_EQ(0, degenerate);
}

TEST(UiLayout, TexturesAreConsistentWithThePixelPool) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load();
    EXPECT_TRUE(!ui.Textures().empty());
    for (int i = 0; i < static_cast<int>(ui.Textures().size()); ++i) {
        const assets::UiTexture& t = ui.Textures()[static_cast<std::size_t>(i)];
        EXPECT_TRUE(t.Valid());
        EXPECT_TRUE(ui.TexturePixels(i) != nullptr);
    }
}

TEST(UiLayout, NoTextureExceedsTheScreen) {
    REQUIRE_EXPORT();
    // The pills overlay is authored at 1920x1080 and was 94% of the pool on its
    // own. Nothing on a 960x544 panel can show more than 960 pixels across.
    const assets::UiLayout ui = Load(false);
    for (const assets::UiTexture& t : ui.Textures()) {
        EXPECT_TRUE(t.width <= 960);
        EXPECT_TRUE(t.height <= 960);
    }
}

TEST(UiLayout, OneBracketTextureIsSharedAcrossTheWholeUi) {
    REQUIRE_EXPORT();
    // 61 of 65 TextureRects are the same 104x104 bracket. If that ever stops
    // being true the atlas assumption in the renderer is wrong.
    const assets::UiLayout ui = Load(false);
    std::map<int, int> uses;
    for (const assets::UiControl& c : ui.Controls()) {
        if (c.type == "TextureRect") ++uses[c.texture];
    }
    int most = 0;
    for (const auto& kv : uses) {
        if (kv.first >= 0 && kv.second > most) most = kv.second;
    }
    EXPECT_TRUE(most >= 60);
}

TEST(UiLayout, TexturePixelsAreRejectedWhenTheIndexIsOutOfRange) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load();
    EXPECT_TRUE(ui.TexturePixels(-1) == nullptr);
    EXPECT_TRUE(ui.TexturePixels(9999) == nullptr);
}

TEST(UiLayout, MissingFileIsReportedNotCrashed) {
    REQUIRE_EXPORT();
    assets::UiLayout ui;
    const assets::UiLayoutError e = ui.Load("no/such/ui.json", nullptr);
    EXPECT_TRUE(e == assets::UiLayoutError::FileNotFound);
    EXPECT_TRUE(ui.Controls().empty());
}

TEST(UiLayout, MissingPixelPoolIsReportedSeparatelyFromAMissingLayout) {
    REQUIRE_EXPORT();
    // Distinguishing these matters: the layout alone is enough for the baker's
    // accounting, and conflating them would hide which file is actually absent.
    assets::UiLayout ui;
    const assets::UiLayoutError e = ui.Load(LayoutPath().c_str(), "no/such/pixels.bin");
    EXPECT_TRUE(e == assets::UiLayoutError::PixelsMissing);
}

TEST(UiLayout, FindsTheEightItemSlots) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load(false);
    const std::vector<int> grabbing =
        ui.FindByPrefix("Camera/dialogue UI/bracket ui/item grabbing");
    EXPECT_TRUE(!grabbing.empty());

    int slots = 0;
    for (int i : grabbing) {
        const assets::UiControl& c = ui.Controls()[static_cast<std::size_t>(i)];
        if (c.type == "Button" && c.name.find("grid place item") != std::string::npos) {
            ++slots;
        }
    }
    EXPECT_EQ(8, slots);
}

TEST(UiLayout, PrefixSearchDoesNotMatchAnUnrelatedSibling) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load(false);
    const std::vector<int> found = ui.FindByPrefix("Camera/dialogue UI/bracket ui/item steal");
    for (int i : found) {
        const std::string& p = ui.Controls()[static_cast<std::size_t>(i)].path;
        EXPECT_TRUE(p.find("item steal") != std::string::npos);
    }
}

TEST(UiLayout, TheSceneWiredItsOwnGamepadNavigation) {
    REQUIRE_EXPORT();
    // The scene's explicit focus_neighbor links make the port playable without
    // a mouse; reproducing them by geometry alone would be a guess.
    const assets::UiLayout ui = Load(false);
    int links = 0;
    for (const assets::UiControl& c : ui.Controls()) {
        if (c.focusTop    >= 0) ++links;
        if (c.focusBottom >= 0) ++links;
        if (c.focusLeft   >= 0) ++links;
        if (c.focusRight  >= 0) ++links;
    }
    EXPECT_TRUE(links >= 180);
}

TEST(UiLayout, EveryFocusLinkPointsAtARealControl) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load(false);
    const int n = static_cast<int>(ui.Controls().size());
    for (const assets::UiControl& c : ui.Controls()) {
        EXPECT_TRUE(c.focusTop    < n);
        EXPECT_TRUE(c.focusBottom < n);
        EXPECT_TRUE(c.focusLeft   < n);
        EXPECT_TRUE(c.focusRight  < n);
    }
}

TEST(UiLayout, ExplicitLinkIsPreferredOverGeometry) {
    REQUIRE_EXPORT();
    // Find a control that has an explicit link whose geometric answer differs,
    // and prove Neighbour follows the scene rather than the maths.
    const assets::UiLayout ui = Load(false);
    bool proved = false;
    for (int i = 0; i < static_cast<int>(ui.Controls().size()); ++i) {
        const assets::UiControl& c = ui.Controls()[static_cast<std::size_t>(i)];
        if (c.focusRight < 0) continue;
        const int geometric = ui.NearestInDirection(i, assets::Direction::Right);
        if (geometric == c.focusRight) continue;
        EXPECT_EQ(c.focusRight, ui.Neighbour(i, assets::Direction::Right));
        proved = true;
        break;
    }
    EXPECT_TRUE(proved);
}

TEST(UiLayout, GeometricNeighbourNeverReturnsAnUnfocusableControl) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load(false);
    const assets::Direction dirs[] = {
        assets::Direction::Up, assets::Direction::Down,
        assets::Direction::Left, assets::Direction::Right};
    for (int i = 0; i < static_cast<int>(ui.Controls().size()); ++i) {
        for (assets::Direction d : dirs) {
            const int to = ui.NearestInDirection(i, d);
            if (to < 0) continue;
            const assets::UiControl& b = ui.Controls()[static_cast<std::size_t>(to)];
            EXPECT_TRUE(b.CanFocus());
        }
    }
}

TEST(UiLayout, GeometricNeighbourMovesInTheRequestedDirection) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load(false);
    int checked = 0;
    for (int i = 0; i < static_cast<int>(ui.Controls().size()); ++i) {
        const assets::UiControl& a = ui.Controls()[static_cast<std::size_t>(i)];
        const int right = ui.NearestInDirection(i, assets::Direction::Right);
        if (right < 0) continue;
        const assets::UiControl& b = ui.Controls()[static_cast<std::size_t>(right)];
        EXPECT_TRUE(b.CentreX() > a.CentreX());
        ++checked;
    }
    EXPECT_TRUE(checked > 0);
}

TEST(UiLayout, NeighbourOfAnInvalidIndexIsRejected) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load(false);
    EXPECT_EQ(-1, ui.Neighbour(-1, assets::Direction::Up));
    EXPECT_EQ(-1, ui.Neighbour(99999, assets::Direction::Up));
}

TEST(UiLayout, DecorativeControlsAreNotFocusTargets) {
    REQUIRE_EXPORT();
    // The brackets are TextureRects drawn under a Button. If one could take
    // focus, navigation would stop on a highlight that does nothing.
    const assets::UiLayout ui = Load(false);
    for (const assets::UiControl& c : ui.Controls()) {
        if (c.type == "TextureRect") EXPECT_TRUE(!c.CanFocus());
    }
}

TEST(UiLayout, ContainsIsHalfOpenSoAdjacentRectsDoNotOverlap) {
    assets::UiControl c;
    c.x = 10.0f; c.y = 20.0f; c.w = 30.0f; c.h = 40.0f;
    EXPECT_TRUE(c.Contains(10.0f, 20.0f));
    EXPECT_TRUE(c.Contains(39.9f, 59.9f));
    EXPECT_TRUE(!c.Contains(40.0f, 40.0f));
    EXPECT_TRUE(!c.Contains(20.0f, 60.0f));
    EXPECT_TRUE(!c.Contains(9.9f, 40.0f));
}

TEST(UiLayout, NoFocusableControlIsVisibleAtSceneLoad) {
    REQUIRE_EXPORT();
    // The whole reason navigation must not filter on the exported visibility:
    // all 68 focusable controls are hidden until their screen opens, so a
    // visibility filter would make every direction return nothing, forever.
    const assets::UiLayout ui = Load(false);
    int focusable = 0;
    int focusableAndVisible = 0;
    for (const assets::UiControl& c : ui.Controls()) {
        if (!c.CanFocus()) continue;
        ++focusable;
        if (c.visibleInTree) ++focusableAndVisible;
    }
    EXPECT_EQ(68, focusable);
    EXPECT_EQ(0, focusableAndVisible);
}

TEST(UiLayout, RestrictedNavigationStaysInsideTheOpenScreen) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load(false);
    std::vector<int> screen;
    for (int i : ui.FindByPrefix("Camera/dialogue UI/bracket ui/item grabbing")) {
        if (ui.Controls()[static_cast<std::size_t>(i)].CanFocus()) screen.push_back(i);
    }
    ASSERT_FALSE(screen.empty());

    const assets::Direction dirs[] = {
        assets::Direction::Up, assets::Direction::Down,
        assets::Direction::Left, assets::Direction::Right};
    int moves = 0;
    for (int from : screen) {
        for (assets::Direction d : dirs) {
            const int to = ui.Neighbour(from, d, screen);
            if (to < 0) continue;
            // Never leaves the screen it was given.
            EXPECT_NE(std::find(screen.begin(), screen.end(), to), screen.end());
            ++moves;
        }
    }
    EXPECT_GT(moves, 0);
}

TEST(UiLayout, RestrictedNavigationFallsBackWhenTheLinkIsOffScreen) {
    REQUIRE_EXPORT();
    // A scene link that points at a control which is not currently on screen
    // must not dead-end the d-pad.
    const assets::UiLayout ui = Load(false);
    std::vector<int> screen;
    for (int i : ui.FindByPrefix("Camera/dialogue UI/bracket ui/item grabbing")) {
        if (ui.Controls()[static_cast<std::size_t>(i)].CanFocus()) screen.push_back(i);
    }
    ASSERT_GE(static_cast<int>(screen.size()), 2);

    // Two members only: most explicit links now point outside the set.
    const std::vector<int> pair = {screen[0], screen[1]};
    for (int from : pair) {
        for (assets::Direction d : {assets::Direction::Left, assets::Direction::Right,
                                    assets::Direction::Up, assets::Direction::Down}) {
            const int to = ui.Neighbour(from, d, pair);
            if (to < 0) continue;
            EXPECT_TRUE(to == pair[0] || to == pair[1]);
            EXPECT_NE(to, from);
        }
    }
}

TEST(UiLayout, RestrictedNavigationIgnoresOutOfRangeCandidates) {
    REQUIRE_EXPORT();
    const assets::UiLayout ui = Load(false);
    const std::vector<int> junk = {-5, 99999};
    EXPECT_EQ(-1, ui.NearestInDirection(0, assets::Direction::Right, junk));
}

}  // namespace

// Each texture reads into a buffer of its own, straight from the file, and
// the pixels can go once they are on the GPU. ui_textures_menu.bin (17.6 MB)
// read back into one vector, then copied into a second, fails with
// std::bad_alloc in a fragmented heap on the way back to the menu.
TEST(UiLayout, EachTextureHasItsOwnBufferAndCanBeReleased) {
    REQUIRE_EXPORT();
    assets::UiLayout ui = Load();
    std::ifstream in(PixelsPath(), std::ios::binary);
    const std::vector<char> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    for (int i = 0; i < static_cast<int>(ui.Textures().size()); ++i) {
        const assets::UiTexture& t = ui.Textures()[static_cast<std::size_t>(i)];
        ASSERT_NE(ui.TexturePixels(i), nullptr) << i;
        EXPECT_EQ(std::memcmp(ui.TexturePixels(i), file.data() + t.offset, t.bytes), 0) << i;
    }

    ui.ReleasePixels();
    for (int i = 0; i < static_cast<int>(ui.Textures().size()); ++i) EXPECT_EQ(ui.TexturePixels(i), nullptr);
}
