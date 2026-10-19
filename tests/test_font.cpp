// The font atlas and text layout.
//
// Run against the real export. `fake receipt.otf` is monospaced at every size
// the UI uses, which several assertions below rely on, and which is asserted
// directly, so if a re-export ever changes it the reason is visible.
#include "assets/font.h"
#include "assets/label3d.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {

std::string JsonPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_fonts.json";
}
std::string PixelsPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_font_atlas.bin";
}

bool Exported() {
    std::FILE* f = std::fopen(JsonPath().c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

#define REQUIRE_EXPORT() \
    if (!Exported()) GTEST_SKIP() << "run tools/godot/export_fonts.gd first"

assets::FontAtlas Load(bool withPixels = false) {
    assets::FontAtlas a;
    const std::string px = PixelsPath();
    EXPECT_STREQ("ok", assets::FontErrorText(
        a.Load(JsonPath().c_str(), withPixels ? px.c_str() : nullptr)));
    return a;
}

const char* kFace23 = "res://fonts/fake receipt.otf @ 23";

// The 2D interface's faces: six sizes of fake receipt plus one embedded face.
// Label3D faces sit beside them in the atlas; these must survive that.
const char* const kUiFaces[] = {
    "res://fonts/fake receipt.otf @ 23", "res://fonts/fake receipt.otf @ 16",
    "res://fonts/fake receipt.otf @ 56", "res://fonts/fake receipt.otf @ 37",
    "res://fonts/fake receipt.otf @ 71", "res://fonts/fake receipt.otf @ 18",
    "(embedded) @ 16",
};

bool IsUiFace(const std::string& key) {
    for (const char* k : kUiFaces) {
        if (key == k) return true;
    }
    return false;
}

TEST(Font, LoadsEveryFaceTheUiAsksFor) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    for (const char* k : kUiFaces) EXPECT_GE(a.Find(k), 0) << "lost UI face " << k;
    EXPECT_GE(a.Find(kFace23), 0);
}

TEST(Font, EveryUiFaceCarriesTheFullPrintableAsciiRange) {
    REQUIRE_EXPORT();
    // Authored Label text uses 32 characters, but dialogue is built at runtime.
    // A face missing 'z' fails only when the game first says a word with one.
    //
    // Label3D faces are partial on purpose (their text is authored and fixed,
    // and the full range at size 232 would not fit the atlas), so they are held
    // to a different rule below.
    const assets::FontAtlas a = Load();
    for (const assets::FontFace& f : a.Faces()) {
        if (!IsUiFace(f.key)) continue;
        for (int c = 32; c <= 126; ++c) {
            EXPECT_NE(nullptr, f.Find(c))
                << "face " << f.key << " has no glyph for '" << char(c) << "'";
        }
    }
}

TEST(Font, EveryGlyphLiesInsideTheAtlas) {
    REQUIRE_EXPORT();
    // A glyph reaching past the page samples whatever follows it in memory.
    const assets::FontAtlas a = Load();
    ASSERT_GT(a.AtlasWidth(), 0);
    ASSERT_GT(a.AtlasHeight(), 0);
    for (const assets::FontFace& f : a.Faces()) {
        for (const assets::Glyph& g : f.glyphs) {
            if (!g.HasPixels()) continue;
            EXPECT_GE(g.x, 0);
            EXPECT_GE(g.y, 0);
            EXPECT_LE(g.x + g.w, a.AtlasWidth());
            EXPECT_LE(g.y + g.h, a.AtlasHeight());
        }
    }
}

TEST(Font, AtlasIsOnePageSoTextNeverSwitchesTexture) {
    REQUIRE_EXPORT();
    // Godot's own cache was 9 pages and 7.50 MB. Repacked it is one page, which
    // is what lets the whole interface stay a small number of draw calls.
    const assets::FontAtlas a = Load(true);
    const std::size_t expect =
        static_cast<std::size_t>(a.AtlasWidth()) *
        static_cast<std::size_t>(a.AtlasHeight()) *
        static_cast<std::size_t>(assets::kFontAtlasChannels);
    EXPECT_EQ(expect, a.Pixels().size());
    EXPECT_LE(a.Pixels().size(), 2u * 1024u * 1024u);
}

TEST(Font, LuminanceIsWhiteAndCoverageIsInAlpha) {
    REQUIRE_EXPORT();
    // GL_LUMINANCE_ALPHA with L pinned to white is what lets `texel * colour`
    // tint text with no work in the fragment shader.
    const assets::FontAtlas a = Load(true);
    ASSERT_FALSE(a.Pixels().empty());
    int nonWhiteLuminance = 0;
    int anyCoverage = 0;
    for (std::size_t i = 0; i + 1 < a.Pixels().size(); i += 2) {
        if (a.Pixels()[i] != 255) ++nonWhiteLuminance;
        if (a.Pixels()[i + 1] != 0) ++anyCoverage;
    }
    EXPECT_EQ(0, nonWhiteLuminance);
    EXPECT_GT(anyCoverage, 0) << "the atlas is entirely transparent";
}

TEST(Font, MissingFileIsReportedNotCrashed) {
    assets::FontAtlas a;
    EXPECT_EQ(assets::FontError::FileNotFound,
              a.Load("no/such/fonts.json", nullptr));
    EXPECT_TRUE(a.Faces().empty());
    EXPECT_EQ(a.LoadNote(), "no/such/fonts.json: cannot open");
}

// The note says which read failed and how far it got (a Vita3K boot once
// reported "not found" for a file it had opened).
TEST(Font, TheLoadNoteSaysHowMuchOfTheMetricsWasRead) {
    const std::string empty = std::string(BSHT_BINARY_DIR) + "/font_note_empty.json";
    std::FILE* f = std::fopen(empty.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fclose(f);
    assets::FontAtlas a;
    EXPECT_EQ(a.Load(empty.c_str(), nullptr), assets::FontError::FileNotFound);
    EXPECT_EQ(a.LoadNote(), empty + ": read 0 of 0 bytes");
    std::remove(empty.c_str());
}

TEST(Font, MissingAtlasIsReportedSeparatelyFromMissingMetrics) {
    REQUIRE_EXPORT();
    assets::FontAtlas a;
    EXPECT_EQ(assets::FontError::PixelsMissing,
              a.Load(JsonPath().c_str(), "no/such/atlas.bin"));
}

TEST(Font, TheUiFontIsMonospaced) {
    REQUIRE_EXPORT();
    // Several layout expectations below depend on this. Asserting it means a
    // re-export that changed fonts fails here rather than somewhere confusing.
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);
    const assets::FontFace& f = a.Faces()[static_cast<std::size_t>(fi)];
    const float adv = f.Find('A')->advance;
    EXPECT_GT(adv, 0.0f);
    for (int c = 33; c <= 126; ++c) {
        EXPECT_FLOAT_EQ(adv, f.Find(c)->advance) << "char '" << char(c) << "'";
    }
}

// Godot 4.1 Label, text_overrun_behavior TRIM_CHAR (text_server_adv.cpp
// shaped_text_overrun_trim_to_width): each line keeps its longest prefix whose
// advance fits the width; a line that fits is untouched. v2.2's ending stats
// trim their 64 dots this way.
TEST(Font, TrimToWidthKeepsTheLongestPrefixThatFits) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);
    const float adv = a.MeasureWidth(fi, "A");
    EXPECT_EQ(a.TrimToWidth(fi, "ABCDEF", adv * 3.0f), "ABC");
    EXPECT_EQ(a.TrimToWidth(fi, "ABCDEF", adv * 3.5f), "ABC");
    EXPECT_EQ(a.TrimToWidth(fi, "ABCDEF", adv * 6.0f), "ABCDEF") << "a line that fits stays";
    EXPECT_EQ(a.TrimToWidth(fi, "ABCDEF\nGH", adv * 2.0f), "AB\nGH") << "each line on its own";
    EXPECT_EQ(a.TrimToWidth(fi, "ABC", 0.0f), "ABC") << "Godot does not trim to a width of 0";
}

TEST(Font, WidthGrowsWithCharacterCount) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);
    EXPECT_FLOAT_EQ(0.0f, a.MeasureWidth(fi, ""));
    EXPECT_LT(a.MeasureWidth(fi, "AB"), a.MeasureWidth(fi, "ABC"));
}

TEST(Font, WidthOfAMultilineStringIsItsLongestLine) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);
    EXPECT_FLOAT_EQ(a.MeasureWidth(fi, "ABCDE"),
                    a.MeasureWidth(fi, "AB\nABCDE\nA"));
}

TEST(Font, HeightCountsLinesNotCharacters) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);
    const assets::FontFace& f = a.Faces()[static_cast<std::size_t>(fi)];
    EXPECT_EQ(3, a.LineCount("a\nb\nc"));
    ASSERT_FALSE(f.lineMetrics.empty());
    const float line = f.lineMetrics[0].ascent + f.lineMetrics[0].descent + 3.0f;
    EXPECT_FLOAT_EQ(3.0f * line, a.MeasureHeight(fi, "a\nb\nc"));
}

TEST(Font, LayoutEmitsOneQuadPerVisibleGlyph) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);

    std::vector<assets::TextQuad> q;
    a.Layout(fi, "ABC", 0, 0, 200, 50, assets::AlignH::Left,
             assets::AlignV::Top, q);
    EXPECT_EQ(3u, q.size());
    for (const assets::TextQuad& t : q) {
        EXPECT_GT(t.w, 0.0f);
        EXPECT_GT(t.h, 0.0f);
        // UVs are normalised into the single page.
        EXPECT_GE(t.u0, 0.0f);
        EXPECT_LE(t.u1, 1.0f);
        EXPECT_GE(t.v0, 0.0f);
        EXPECT_LE(t.v1, 1.0f);
        EXPECT_LT(t.u0, t.u1);
        EXPECT_LT(t.v0, t.v1);
    }
}

TEST(Font, LayoutAppendsRatherThanClearing) {
    REQUIRE_EXPORT();
    // One buffer collects a whole screen's text; clearing would drop the rest.
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    std::vector<assets::TextQuad> q;
    a.Layout(fi, "AB", 0, 0, 200, 50, assets::AlignH::Left, assets::AlignV::Top, q);
    const std::size_t first = q.size();
    a.Layout(fi, "CD", 0, 0, 200, 50, assets::AlignH::Left, assets::AlignV::Top, q);
    EXPECT_EQ(first * 2, q.size());
}

TEST(Font, ASpaceAdvancesThePenAndDrawsNothing) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);
    const assets::FontFace& f = a.Faces()[static_cast<std::size_t>(fi)];
    ASSERT_NE(nullptr, f.Find(' '));
    EXPECT_GT(f.Find(' ')->advance, 0.0f);

    std::vector<assets::TextQuad> withSpace;
    std::vector<assets::TextQuad> without;
    a.Layout(fi, "A B", 0, 0, 400, 50, assets::AlignH::Left, assets::AlignV::Top,
             withSpace);
    a.Layout(fi, "AB", 0, 0, 400, 50, assets::AlignH::Left, assets::AlignV::Top,
             without);
    EXPECT_EQ(without.size(), withSpace.size());
    // ...but the second glyph has been pushed along by the space.
    EXPECT_GT(withSpace[1].x, without[1].x);
}

TEST(Font, CentringPutsTextInTheMiddleOfTheRect) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);

    std::vector<assets::TextQuad> left;
    std::vector<assets::TextQuad> centre;
    std::vector<assets::TextQuad> right;
    a.Layout(fi, "ABC", 0, 0, 300, 50, assets::AlignH::Left,
             assets::AlignV::Top, left);
    a.Layout(fi, "ABC", 0, 0, 300, 50, assets::AlignH::Centre,
             assets::AlignV::Top, centre);
    a.Layout(fi, "ABC", 0, 0, 300, 50, assets::AlignH::Right,
             assets::AlignV::Top, right);
    ASSERT_EQ(3u, left.size());
    EXPECT_LT(left[0].x, centre[0].x);
    EXPECT_LT(centre[0].x, right[0].x);

    // The centred run is as far from the right edge as from the left.
    const float w = a.MeasureWidth(fi, "ABC");
    EXPECT_NEAR((300.0f - w) * 0.5f, centre[0].x - left[0].x, 0.51f);
}

TEST(Font, VerticalAlignmentMovesTheBaseline) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);

    std::vector<assets::TextQuad> top;
    std::vector<assets::TextQuad> mid;
    std::vector<assets::TextQuad> bot;
    a.Layout(fi, "A", 0, 0, 300, 200, assets::AlignH::Left,
             assets::AlignV::Top, top);
    a.Layout(fi, "A", 0, 0, 300, 200, assets::AlignH::Left,
             assets::AlignV::Centre, mid);
    a.Layout(fi, "A", 0, 0, 300, 200, assets::AlignH::Left,
             assets::AlignV::Bottom, bot);
    ASSERT_EQ(1u, top.size());
    EXPECT_LT(top[0].y, mid[0].y);
    EXPECT_LT(mid[0].y, bot[0].y);
}

TEST(Font, TopAlignedTextStaysInsideItsRect) {
    REQUIRE_EXPORT();
    // Glyph offsets are measured from the BASELINE and are negative going up.
    // Getting the sign wrong puts every string a whole ascent above its box.
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);
    const assets::FontFace& f = a.Faces()[static_cast<std::size_t>(fi)];

    std::vector<assets::TextQuad> q;
    a.Layout(fi, "Ag", 100, 50, 300, 200, assets::AlignH::Left,
             assets::AlignV::Top, q);
    ASSERT_FALSE(q.empty());
    for (const assets::TextQuad& t : q) {
        EXPECT_GE(t.y, 50.0f) << "glyph sits above its rect";
        EXPECT_LE(t.y + t.h, 50.0f + f.height + f.descent);
        EXPECT_GE(t.x, 100.0f);
    }
}

TEST(Font, NewlinesStackLinesDownwards) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);
    const assets::FontFace& f = a.Faces()[static_cast<std::size_t>(fi)];

    std::vector<assets::TextQuad> q;
    a.Layout(fi, "A\nA", 0, 0, 300, 200, assets::AlignH::Left,
             assets::AlignV::Top, q);
    ASSERT_EQ(2u, q.size());
    EXPECT_FLOAT_EQ(q[0].x, q[1].x);
    ASSERT_FALSE(f.lineMetrics.empty());
    EXPECT_NEAR(f.lineMetrics[0].ascent + f.lineMetrics[0].descent + 3.0f, q[1].y - q[0].y, 0.01f);
}

TEST(Font, LayoutOfAnInvalidFaceIsRejected) {
    REQUIRE_EXPORT();
    const assets::FontAtlas a = Load();
    std::vector<assets::TextQuad> q;
    a.Layout(-1, "ABC", 0, 0, 300, 50, assets::AlignH::Left,
             assets::AlignV::Top, q);
    a.Layout(9999, "ABC", 0, 0, 300, 50, assets::AlignH::Left,
             assets::AlignV::Top, q);
    EXPECT_TRUE(q.empty());
    EXPECT_FLOAT_EQ(0.0f, a.MeasureWidth(-1, "ABC"));
}

TEST(Font, CharactersOutsideTheExportedRangeAreSkippedNotSubstituted) {
    REQUIRE_EXPORT();
    // Leaving a gap keeps the rest of the line where the author put it; a
    // substituted glyph would silently change the text.
    const assets::FontAtlas a = Load();
    const int fi = a.Find(kFace23);
    ASSERT_GE(fi, 0);
    const assets::FontFace& f = a.Faces()[static_cast<std::size_t>(fi)];
    EXPECT_EQ(nullptr, f.Find(31));
    // A private-use code point: no table has it, so no face does.
    EXPECT_EQ(nullptr, f.Find(0xE000));

    std::vector<assets::TextQuad> q;
    a.Layout(fi, "A\x01\x02" "B", 0, 0, 300, 50, assets::AlignH::Left,
             assets::AlignV::Top, q);
    EXPECT_EQ(2u, q.size());
}

// Godot's Label lays its lines out by the fonts each line's glyphs come from
// (text_server_adv.cpp _shape_substr) plus the label's line_spacing
// (label.cpp NOTIFICATION_DRAW), not by Font.get_height, which takes the
// tallest of every fallback. A face whose own font is 10/4, with a taller
// fallback (14/6) for 'B' and a shorter one (8/2) for 'C'.
std::string WriteLineFont() {
    // One file per test: ctest runs each test in its own process, in parallel.
    const std::string path = std::string(BSHT_BINARY_DIR) + "/font_lines_" +
                             ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".json";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return std::string();
    std::fputs(R"({"atlas_width": 64, "atlas_height": 64, "first_char": 32, "last_char": 126,
 "pages": [{"group": "base", "file": "font_lines.bin", "width": 64, "height": 64}],
 "faces": [{"key": "lines @ 10", "path": "lines", "size": 10, "ascent": 14, "descent": 6, "height": 20,
  "line_metrics": [[10, 4], [14, 6], [8, 2]],
  "glyphs": [
   {"char": 32, "present": true, "w": 0, "h": 0, "ox": 0, "oy": 0, "advance": 5, "x": 0, "y": 0, "page": 0},
   {"char": 65, "present": true, "w": 4, "h": 8, "ox": 0, "oy": -8, "advance": 5, "x": 0, "y": 0, "page": 0},
   {"char": 66, "present": true, "w": 4, "h": 8, "ox": 0, "oy": -8, "advance": 5, "x": 8, "y": 0, "page": 0, "font": 1},
   {"char": 67, "present": true, "w": 4, "h": 8, "ox": 0, "oy": -8, "advance": 5, "x": 16, "y": 0, "page": 0, "font": 2},
   {"char": 68, "present": true, "w": 4, "h": 8, "ox": 0, "oy": -8, "advance": 4.5, "x": 24, "y": 0, "page": 0}
  ]}]})", f);
    std::fclose(f);
    return path;
}

struct LineFont : ::testing::Test {
    assets::FontAtlas atlas;
    std::vector<assets::TextQuad> q;
    void SetUp() override {
        const std::string path = WriteLineFont();
        ASSERT_FALSE(path.empty());
        ASSERT_STREQ("ok", assets::FontErrorText(atlas.Load(path.c_str(), nullptr)));
    }
    void Lay(const char* text, float w, float h, assets::AlignH ah, assets::AlignV av, int spacing) {
        q.clear();
        atlas.Layout(0, text, 0, 0, w, h, ah, av, q, spacing);
    }
};

TEST_F(LineFont, LoadsTheLineMetricsAndEachGlyphsFont) {
    const assets::FontFace& f = atlas.Faces()[0];
    ASSERT_EQ(f.lineMetrics.size(), 3u);
    EXPECT_FLOAT_EQ(f.lineMetrics[0].ascent, 10.0f);
    EXPECT_FLOAT_EQ(f.lineMetrics[0].descent, 4.0f);
    EXPECT_FLOAT_EQ(f.lineMetrics[1].ascent, 14.0f);
    EXPECT_EQ(f.Find('A')->font, 0);
    EXPECT_EQ(f.Find('B')->font, 1);
}

TEST_F(LineFont, LinesAdvanceByTheirOwnFontPlusTheLineSpacing) {
    Lay("A\nA", 100, 100, assets::AlignH::Left, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[0].y, 10.0f - 8.0f);                       // baseline at the ascent
    EXPECT_FLOAT_EQ(q[1].y - q[0].y, 10.0f + 4.0f + 3.0f);       // ascent + descent + spacing
}

TEST_F(LineFont, ALineWithAFallbackGlyphIsAsTallAsThatFont) {
    Lay("B\nA", 100, 100, assets::AlignH::Left, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[0].y, 14.0f - 8.0f);
    EXPECT_FLOAT_EQ(q[1].y, 14.0f + 6.0f + 3.0f + 10.0f - 8.0f);
}

TEST_F(LineFont, ABlankLineIsAsTallAsTheFacesOwnFont) {
    Lay("A\n\nA", 100, 100, assets::AlignH::Left, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[1].y - q[0].y, 2.0f * (10.0f + 4.0f + 3.0f));
}

// Label breaks with BREAK_TRIM_EDGE_SPACES (label.cpp _shape;
// text_server.cpp shaped_text_get_line_breaks): a line ended by a newline
// loses the newline and its trailing spaces, so it is as tall as its own
// glyphs. Only a blank line keeps its newline, the face's own glyph.
TEST_F(LineFont, ALastLineOfFallbackGlyphsTakesOnlyThatFont) {
    Lay("C", 100, 100, assets::AlignH::Left, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q[0].y, 8.0f - 8.0f);
}

TEST_F(LineFont, ALineEndedByANewlineIsAsTallAsItsOwnGlyphs) {
    Lay("C\nA", 100, 100, assets::AlignH::Left, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[0].y, 8.0f - 8.0f);
    EXPECT_FLOAT_EQ(q[1].y, 8.0f + 2.0f + 3.0f + 10.0f - 8.0f);
}

TEST_F(LineFont, TrailingSpacesBeforeANewlineDoNotCount) {
    Lay("C \nA", 100, 100, assets::AlignH::Left, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[0].y, 8.0f - 8.0f);
}

TEST_F(LineFont, TheLastLineKeepsItsTrailingSpaces) {
    Lay("C ", 100, 100, assets::AlignH::Left, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q[0].y, 10.0f - 8.0f);
}

TEST_F(LineFont, ATrailingNewlineAddsNoLine) {
    EXPECT_FLOAT_EQ(atlas.MeasureHeight(0, "A\n", 3), atlas.MeasureHeight(0, "A", 3));
    EXPECT_FLOAT_EQ(atlas.MeasureHeight(0, "A\n\n", 3), atlas.MeasureHeight(0, "A\n", 3) + 10.0f + 4.0f + 3.0f);
    Lay("A\n", 100, 40, assets::AlignH::Left, assets::AlignV::Centre, 3);
    ASSERT_EQ(q.size(), 1u);
    const float withNewline = q[0].y;
    Lay("A", 100, 40, assets::AlignH::Left, assets::AlignV::Centre, 3);
    EXPECT_FLOAT_EQ(withNewline, q[0].y) << "a typed line that just reached its newline must not jump";
}

TEST_F(LineFont, CentringUsesTheLineWidthRoundedUp) {
    // _shaped_text_get_size ceils: int(12.6 - 5) / 2 = 3, where 4.5 would give 4.
    Lay("D", 12.6f, 20, assets::AlignH::Centre, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q[0].x, 3.0f);
    Lay("D", 12.6f, 20, assets::AlignH::Right, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q[0].x, 7.0f);
}

TEST_F(LineFont, ASpaceBringsTheFacesOwnFont) {
    Lay("C C", 100, 100, assets::AlignH::Left, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[0].y, 10.0f - 8.0f);
}

TEST_F(LineFont, LabelsWithoutLineSpacingStackTight) {
    Lay("A\nA", 100, 100, assets::AlignH::Left, assets::AlignV::Top, 0);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[1].y - q[0].y, 14.0f);
}

TEST_F(LineFont, CentringLeavesOutTheLastSpacingInWholePixels) {
    // vbegin = int((21 - (17 - 3)) / 2) = 3; the baseline 3 + 10.
    Lay("A", 100, 21, assets::AlignH::Left, assets::AlignV::Centre, 3);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q[0].y, 3.0f + 10.0f - 8.0f);
}

TEST_F(LineFont, BottomAlignmentLeavesOutTheLastSpacing) {
    // vbegin = int(21 - (17 - 3)) = 7.
    Lay("A", 100, 21, assets::AlignH::Left, assets::AlignV::Bottom, 3);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q[0].y, 7.0f + 10.0f - 8.0f);
}

TEST_F(LineFont, FillSpreadsTheLinesOverTheRect) {
    // vsep = int((40 - (28 - 0)) / (2 - 1)) = 12 between the two lines.
    Lay("A\nA", 100, 40, assets::AlignH::Left, assets::AlignV::Fill, 0);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_FLOAT_EQ(q[0].y, 10.0f - 8.0f);
    EXPECT_FLOAT_EQ(q[1].y - q[0].y, 4.0f + 12.0f + 10.0f);   // descent + vsep + ascent
}

TEST_F(LineFont, CentringAcrossIsInWholePixels) {
    // ofs.x = int(12 - 5) / 2 = 3, not 3.5.
    Lay("A", 12, 20, assets::AlignH::Centre, assets::AlignV::Top, 3);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q[0].x, 3.0f);
}

TEST_F(LineFont, MeasureHeightIsTheLabelsMinimumHeight) {
    // label.cpp _update_visible: every line plus its spacing.
    EXPECT_FLOAT_EQ(atlas.MeasureHeight(0, "B\nA\nA", 0), (14.0f + 6.0f) + 2.0f * (10.0f + 4.0f));
}

TEST(Font, AGlyphNamingAFontTheFaceDoesNotHaveIsRefused) {
    const std::string path = std::string(BSHT_BINARY_DIR) + "/font_bad_row.json";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fputs(R"({"atlas_width": 64, "atlas_height": 64, "first_char": 32, "last_char": 126,
 "faces": [{"key": "bad @ 10", "size": 10, "ascent": 14, "descent": 6, "height": 20,
  "line_metrics": [[10, 4]],
  "glyphs": [{"char": 65, "present": true, "w": 4, "h": 8, "ox": 0, "oy": -8, "advance": 5, "font": 1}]}]})", f);
    std::fclose(f);
    assets::FontAtlas a;
    EXPECT_EQ(a.Load(path.c_str(), nullptr), assets::FontError::Inconsistent);
}

}  // namespace

TEST(Font, EveryLabel3DFaceHasEveryCharacterItsLabelsDraw) {
    REQUIRE_EXPORT();
    // A partial face is only safe if it holds everything its labels say. This is
    // the Label3D half of the full-range rule above.
    const assets::FontAtlas a = Load();
    assets::Labels3D labels;
    ASSERT_TRUE(labels.LoadFromFile(BSHT_SOURCE_DIR "/assets/extracted/labels3d.json"));
    int checked = 0;
    for (const assets::Label3D& l : labels.All()) {
        const int fi = a.Find(assets::Label3DFaceKey(l));
        ASSERT_GE(fi, 0) << l.path << " has no face " << assets::Label3DFaceKey(l);
        const assets::FontFace& f = a.Faces()[static_cast<std::size_t>(fi)];
        for (unsigned char c : l.text) {
            if (c < 32 || c > 126) continue;   // newlines, and nothing else is authored
            const char upper = (l.uppercase && c >= 'a' && c <= 'z') ? static_cast<char>(c - 32)
                                                                     : static_cast<char>(c);
            EXPECT_NE(nullptr, f.Find(upper))
                << f.key << " lacks '" << upper << "' for " << l.path;
        }
        ++checked;
    }
    EXPECT_EQ(checked, static_cast<int>(labels.All().size()));
}
