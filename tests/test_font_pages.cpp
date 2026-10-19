// The glyph atlas has several pages. Every language draws, CJK included, from
// Godot's own rasteriser (export_fonts.gd); the base pages stay resident and a
// CJK locale's pages are loaded only while it is chosen. Switching to a CJK
// group and back releases its pages, and a character with no glyph draws the
// rest and logs once, never crashes.
//
// A synthetic atlas: 'A' on page 0, 'e-acute' (base, non-ASCII) on page 1, and
// '日' in group "JA" on page 2. Text is UTF-8 throughout.
#include "assets/font.h"

#include <cstdio>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

// One directory per test: ctest runs each test in its own process, in parallel.
std::string kDir;

void WritePage(const std::string& name, int w, int h) {
    std::vector<unsigned char> px(static_cast<size_t>(w * h * 2), 255);
    std::FILE* f = std::fopen((kDir + "/" + name).c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite(px.data(), 1, px.size(), f);
    std::fclose(f);
}

void WriteFixture() {
#ifdef _WIN32
    std::system(("mkdir \"" + kDir + "\" 2>nul").c_str());
#else
    std::system(("mkdir -p '" + kDir + "'").c_str());
#endif
    WritePage("p0.bin", 64, 64);
    WritePage("p1.bin", 64, 32);
    WritePage("ja0.bin", 32, 32);
    const char* json = R"({
  "first_char": 32, "last_char": 126, "atlas_width": 64, "atlas_height": 64,
  "pages": [
    {"group": "base", "file": "p0.bin", "width": 64, "height": 64},
    {"group": "base", "file": "p1.bin", "width": 64, "height": 32},
    {"group": "JA", "file": "ja0.bin", "width": 32, "height": 32}
  ],
  "faces": [{
    "key": "f @ 10", "path": "f", "size": 10, "ascent": 8, "descent": 2, "height": 10,
    "glyphs": [
      {"char": 32, "present": true, "w": 0, "h": 0, "ox": 0, "oy": 0, "advance": 5, "x": 0, "y": 0, "page": 0},
      {"char": 65, "present": true, "w": 4, "h": 8, "ox": 0, "oy": -8, "advance": 5, "x": 8, "y": 16, "page": 0}
    ],
    "extra": [
      {"char": 233, "present": true, "w": 4, "h": 8, "ox": 0, "oy": -8, "advance": 5, "x": 32, "y": 16, "page": 1}
    ],
    "groups": {"JA": [
      {"char": 26085, "present": true, "w": 8, "h": 8, "ox": 0, "oy": -8, "advance": 10, "x": 16, "y": 8, "page": 2}
    ]}
  }]
})";
    std::FILE* f = std::fopen((kDir + "/fonts.json").c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fputs(json, f);
    std::fclose(f);
}

struct FontPages : ::testing::Test {
    assets::FontAtlas atlas;
    std::vector<std::string> logged;

    void SetUp() override {
        kDir = std::string(BSHT_BINARY_DIR) + "/font_pages_" +
               ::testing::UnitTest::GetInstance()->current_test_info()->name();
        WriteFixture();
        atlas.SetLog([this](const std::string& s) { logged.push_back(s); });
        ASSERT_STREQ("ok", assets::FontErrorText(
            atlas.Load((kDir + "/fonts.json").c_str(), (kDir + "/p0.bin").c_str())));
    }

    std::vector<assets::TextQuad> Lay(const std::string& text) {
        std::vector<assets::TextQuad> q;
        atlas.Layout(0, text, 0, 0, 200, 20, assets::AlignH::Left, assets::AlignV::Top, q);
        return q;
    }
};

TEST_F(FontPages, BasePagesAreResidentAndTheGroupIsNot) {
    ASSERT_EQ(atlas.Pages().size(), 3u);
    EXPECT_FALSE(atlas.Pages()[0].pixels.empty());
    EXPECT_FALSE(atlas.Pages()[1].pixels.empty());
    EXPECT_TRUE(atlas.Pages()[2].pixels.empty());
    EXPECT_EQ(atlas.AtlasWidth(), 64);  // page 0, for the loading screen
}

TEST_F(FontPages, AGlyphReportsItsPageAndUvsAreThatPagesSize) {
    const auto q = Lay("A\xC3\xA9");  // "Aé"
    ASSERT_EQ(q.size(), 2u);
    EXPECT_EQ(q[0].page, 0);
    EXPECT_EQ(q[1].page, 1);
    EXPECT_FLOAT_EQ(q[1].u0, 32.0f / 64.0f);
    EXPECT_FLOAT_EQ(q[1].v0, 16.0f / 32.0f);  // page 1 is 32 high
    EXPECT_FLOAT_EQ(q[1].x, 5.0f);
}

TEST_F(FontPages, AGroupGlyphDrawsOnlyWhileTheGroupIsLoaded) {
    const std::string nichi = "\xE6\x97\xA5";  // 日
    EXPECT_TRUE(Lay(nichi).empty());
    ASSERT_TRUE(atlas.LoadGroup("JA"));
    EXPECT_EQ(atlas.ActiveGroup(), "JA");
    EXPECT_FALSE(atlas.Pages()[2].pixels.empty());
    const auto q = Lay("A" + nichi);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_EQ(q[1].page, 2);
    EXPECT_FLOAT_EQ(q[1].u0, 16.0f / 32.0f);
    atlas.UnloadGroup();
    EXPECT_EQ(atlas.ActiveGroup(), "");
    EXPECT_TRUE(atlas.Pages()[2].pixels.empty());
    EXPECT_EQ(Lay("A" + nichi).size(), 1u);
}

// once a page is on the GPU its CPU copy is dead weight (5 MB of
// base pages held for the whole run on a 53 MB-free Vita). ReleasePixels drops
// every resident copy; layout, which needs only metrics, is unaffected.
TEST_F(FontPages, ReleasingThePixelsKeepsLayoutWorking) {
    const std::string nichi = "\xE6\x97\xA5";  // 日
    ASSERT_TRUE(atlas.LoadGroup("JA"));
    atlas.ReleasePixels();
    for (const assets::FontPage& p : atlas.Pages()) {
        EXPECT_TRUE(p.pixels.empty()) << p.file;
        EXPECT_EQ(p.pixels.capacity(), 0u) << p.file << ": cleared but not freed";
    }
    EXPECT_EQ(atlas.ActiveGroup(), "JA");
    const auto q = Lay("A\xC3\xA9" + nichi);
    ASSERT_EQ(q.size(), 3u);
    EXPECT_EQ(q[2].page, 2);
}

TEST_F(FontPages, AnUnknownGroupIsRefused) {
    EXPECT_FALSE(atlas.LoadGroup("KO"));
    EXPECT_EQ(atlas.ActiveGroup(), "");
}

TEST_F(FontPages, AMissingGlyphIsSkippedAndLoggedOnce) {
    const std::string text = "A\xE2\x98\x83" "A";  // A, snowman, A
    auto q = Lay(text);
    EXPECT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[1].x, 5.0f);  // a gap does not advance the pen
    Lay(text);
    int snowman = 0;
    for (const auto& l : logged) snowman += l.find("U+2603") != std::string::npos;
    EXPECT_EQ(snowman, 1);
}

TEST_F(FontPages, BrokenUtf8NeverCrashes) {
    const auto q = Lay(std::string("A\xE6\x97", 3));  // truncated sequence
    EXPECT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(atlas.MeasureWidth(0, "A\xC3\xA9"), 10.0f);
}

}  // namespace
