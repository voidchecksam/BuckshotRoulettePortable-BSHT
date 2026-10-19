// The screen shown when the game data is missing or wrong: font8x8 (public
// domain) compiled in, because the game's own fonts are game data.
#include "app/data_screen.h"

#include "app/data_check.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

// The atlas texel (x, y) is lit.
bool Lit(const std::vector<uint8_t>& atlas, int x, int y) {
    return atlas[static_cast<std::size_t>((y * app::kDataAtlasWidth + x) * 4 + 3)] != 0;
}

bool CellHasInk(const std::vector<uint8_t>& atlas, unsigned char c) {
    const int cx = (c % 16) * 8, cy = (c / 16) * 8;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (Lit(atlas, cx + x, cy + y)) return true;
    return false;
}

TEST(DataScreen, AtlasHoldsTheAsciiGlyphs) {
    const std::vector<uint8_t> atlas = app::DataScreenAtlas();
    ASSERT_EQ(atlas.size(), static_cast<std::size_t>(app::kDataAtlasWidth * app::kDataAtlasHeight * 4));
    EXPECT_EQ(app::kDataAtlasWidth, 128);
    EXPECT_EQ(app::kDataAtlasHeight, 64);
    EXPECT_TRUE(CellHasInk(atlas, 'A'));
    EXPECT_FALSE(CellHasInk(atlas, ' '));
}

TEST(DataScreen, BitZeroIsTheLeftmostPixel) {
    // font8x8's 'I' top row is 0x1E: pixels 1-4 lit, 0 and 5-7 clear.
    const std::vector<uint8_t> atlas = app::DataScreenAtlas();
    const int cx = ('I' % 16) * 8, cy = ('I' / 16) * 8;
    EXPECT_FALSE(Lit(atlas, cx + 0, cy));
    for (int x = 1; x <= 4; ++x) EXPECT_TRUE(Lit(atlas, cx + x, cy)) << x;
    for (int x = 5; x <= 7; ++x) EXPECT_FALSE(Lit(atlas, cx + x, cy)) << x;
}

TEST(DataScreen, OneLineIsCentred) {
    const std::vector<app::GlyphQuad> q = app::LayoutDataScreen({"ABC"}, 960, 540, 3);
    ASSERT_EQ(q.size(), 3u);
    EXPECT_FLOAT_EQ(q[0].w, 24.0f);
    EXPECT_FLOAT_EQ(q[0].h, 24.0f);
    EXPECT_FLOAT_EQ(q[0].x, 480.0f - 36.0f);
    EXPECT_FLOAT_EQ(q[1].x - q[0].x, 24.0f);
    EXPECT_FLOAT_EQ(q[0].y, 270.0f - 12.0f);
}

TEST(DataScreen, GlyphUvsPointAtTheirCell) {
    const std::vector<app::GlyphQuad> q = app::LayoutDataScreen({"A"}, 960, 540, 2);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q[0].u0, ('A' % 16) * 8 / 128.0f);
    EXPECT_FLOAT_EQ(q[0].v0, ('A' / 16) * 8 / 64.0f);
    EXPECT_FLOAT_EQ(q[0].u1 - q[0].u0, 8 / 128.0f);
    EXPECT_FLOAT_EQ(q[0].v1 - q[0].v0, 8 / 64.0f);
}

TEST(DataScreen, LinesStackWithAGap) {
    const std::vector<app::GlyphQuad> q = app::LayoutDataScreen({"A", "B"}, 960, 540, 2);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_GT(q[1].y, q[0].y + q[0].h);   // a gap between lines
}

TEST(DataScreen, SpacesTakeRoomButDrawNothing) {
    const std::vector<app::GlyphQuad> q = app::LayoutDataScreen({"A B"}, 960, 540, 2);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[1].x - q[0].x, 32.0f);
}

TEST(DataScreen, UnknownCharactersDrawAsQuestionMarks) {
    const std::vector<app::GlyphQuad> q = app::LayoutDataScreen({"\xC3\xA9"}, 960, 540, 2);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[0].u0, ('?' % 16) * 8 / 128.0f);
}

TEST(DataScreen, NothingLeavesThePanel) {
    const std::vector<app::GlyphQuad> q = app::LayoutDataScreen({std::string(200, 'W')}, 960, 540, 2);
    ASSERT_FALSE(q.empty());
    for (const app::GlyphQuad& g : q) {
        EXPECT_GE(g.x, 0.0f);
        EXPECT_LE(g.x + g.w, 960.0f);
    }
}

// The folder the player has to fill is the line that matters most, and a zip
// unpacked under Downloads makes it long: it wraps, at a separator, instead of
// losing its end at the panel's edge.
TEST(DataScreen, ALongPathWrapsWithNothingCut) {
    const std::string path =
        "No game data in C:\\Users\\Someone\\Downloads\\bsht-desktop-v1.0.0-windows\\bsht-desktop\\game";
    const std::vector<app::GlyphQuad> q = app::LayoutDataScreen({path}, 960, 544, 2);
    std::size_t ink = 0;
    for (char c : path) ink += c != ' ';
    EXPECT_EQ(q.size(), ink);   // every character drawn
    float firstRow = q.front().y, lastRow = q.front().y;
    for (const app::GlyphQuad& g : q) {
        EXPECT_LE(g.x + g.w, 960.0f);
        lastRow = std::max(lastRow, g.y);
    }
    EXPECT_GT(lastRow, firstRow);   // on more than one row
    // "game" stays whole on the last row.
    std::size_t onLast = 0;
    for (const app::GlyphQuad& g : q) onLast += g.y == lastRow;
    EXPECT_GE(onLast, 4u);
}

TEST(DataScreen, LinesEndWithHowToLeave) {
    app::GameDataCheck check;
    check.lines = {"No game data in ux0:data/bsht/game", "Run bsht-extract and copy its game folder."};
    const std::vector<std::string> lines = app::DataScreenLines(check, "Press START to exit");
    ASSERT_GE(lines.size(), 3u);
    EXPECT_EQ(lines.front(), check.lines.front());
    EXPECT_EQ(lines.back(), "Press START to exit");
}

// The desktop has no START: its screen names its own way out.
TEST(DataScreen, TheWayOutIsThePlatformsOwn) {
    app::GameDataCheck check;
    check.lines = {"No game data in game"};
    EXPECT_EQ(app::DataScreenLines(check, "Close the window to exit").back(), "Close the window to exit");
}

}  // namespace
