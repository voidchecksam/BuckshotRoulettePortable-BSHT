// Label3D layout, pinned to Godot 4.1's Label3D::_shape and
// _generate_glyph_surfaces, against the real extracted labels and atlas.
#include "assets/font.h"
#include "assets/label3d.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace {

const std::string kRoot = BSHT_SOURCE_DIR "/assets/extracted/";

assets::Labels3D Labels() {
    assets::Labels3D l;
    EXPECT_TRUE(l.LoadFromFile((kRoot + "labels3d.json").c_str()));
    return l;
}

bool Atlas(assets::FontAtlas& a) {
    return a.Load((kRoot + "ui_fonts.json").c_str(), (kRoot + "ui_font_atlas.bin").c_str()) ==
           assets::FontError::None;
}

const char* kShootDealer = "ui parent_shooting decision/text dealer";

}  // namespace

TEST(Label3D, LoadsEveryLabelInTheScene) {
    BSHT_NEEDS_FILE(kRoot + "labels3d.json");
    const assets::Labels3D l = Labels();
    EXPECT_EQ(l.All().size(), 99u);
    const int i = l.Find(kShootDealer);
    ASSERT_GE(i, 0);
    const assets::Label3D& d = l.All()[static_cast<std::size_t>(i)];
    EXPECT_EQ(d.fontSize, 232);
    EXPECT_FLOAT_EQ(d.transparency, 1.0f) << "the shooting choice starts invisible";
    EXPECT_FLOAT_EQ(d.outlineModulate[3], 0.0f);
}

TEST(Label3D, AMissingFaceLaysOutNothing) {
    assets::FontAtlas atlas;
    if (!Atlas(atlas)) GTEST_SKIP() << "no font atlas";
    assets::Label3D l;
    l.text = "X";
    l.font = "res://no such font.ttf";
    std::vector<assets::LabelQuad> q;
    EXPECT_FALSE(assets::LayoutLabel3D(atlas, l, q));
    EXPECT_TRUE(q.empty());
}

TEST(Label3D, CentresTheShootingChoiceOnItsOrigin) {
    assets::FontAtlas atlas;
    if (!Atlas(atlas)) GTEST_SKIP() << "no font atlas";
    const assets::Labels3D labels = Labels();
    assets::Label3D l = labels.All()[static_cast<std::size_t>(labels.Find(kShootDealer))];
    const int fi = atlas.Find(assets::Label3DFaceKey(l));
    if (fi < 0) GTEST_SKIP() << "atlas lacks " << assets::Label3DFaceKey(l) << " -- run export_fonts.gd";
    const assets::FontFace& face = atlas.Faces()[static_cast<std::size_t>(fi)];

    l.transparency = 0.0f;
    std::vector<assets::LabelQuad> q;
    ASSERT_TRUE(assets::LayoutLabel3D(atlas, l, q));
    ASSERT_EQ(q.size(), l.text.size()) << "one quad per inked glyph, no outline";

    float width = 0.0f;
    for (char c : l.text) width += face.Find(c)->advance;
    width *= l.pixelSize;

    // HORIZONTAL_ALIGNMENT_CENTER: offset.x = -line_width / 2.
    const assets::Glyph* first = face.Find(l.text[0]);
    EXPECT_NEAR(q[0].x0, -width / 2.0f + first->ox * l.pixelSize, 1e-5f);

    // VERTICAL_ALIGNMENT_CENTER on one line: baseline = (descent - ascent) / 2,
    // the metrics of the line's own font (text_server_adv.cpp _shape_substr),
    // not Font.get_ascent, which takes the tallest fallback.
    ASSERT_FALSE(face.lineMetrics.empty());
    const assets::FontFace::LineMetric own = face.lineMetrics[0];
    const float baseline = (own.descent - own.ascent) * l.pixelSize / 2.0f;
    EXPECT_NEAR(q[0].yTop, baseline - first->oy * l.pixelSize, 1e-5f);
    EXPECT_NEAR(q[0].yTop - q[0].yBottom, first->h * l.pixelSize, 1e-5f);
    EXPECT_LT(q[0].yBottom, q[0].yTop) << "+Y is up in label space";
}

// The health counter's DOUBLE uses fake receipt @ 132, whose fallbacks (154/39)
// are taller than the font itself (129/31): centred on the font's own metrics.
TEST(Label3D, CentresOnTheLinesOwnFontNotTheTallestFallback) {
    assets::FontAtlas atlas;
    if (!Atlas(atlas)) GTEST_SKIP() << "no font atlas";
    const assets::Labels3D labels = Labels();
    const assets::Label3D* found = nullptr;
    for (const assets::Label3D& x : labels.All()) {
        const std::string tail = "indicator_double or nothing";
        if (x.path.size() >= tail.size() && x.path.compare(x.path.size() - tail.size(), tail.size(), tail) == 0) found = &x;
    }
    ASSERT_NE(found, nullptr);
    assets::Label3D l = *found;
    const int fi = atlas.Find(assets::Label3DFaceKey(l));
    ASSERT_GE(fi, 0);
    const assets::FontFace& face = atlas.Faces()[static_cast<std::size_t>(fi)];
    ASSERT_FALSE(face.lineMetrics.empty());
    const assets::FontFace::LineMetric own = face.lineMetrics[0];
    ASSERT_NE(own.ascent, face.ascent);
    ASSERT_EQ(l.alignV, 1);

    l.transparency = 0.0f;
    l.outlineModulate[3] = 0.0f;   // the fill pass only
    std::vector<assets::LabelQuad> q;
    ASSERT_TRUE(assets::LayoutLabel3D(atlas, l, q));
    ASSERT_FALSE(q.empty());
    const assets::Glyph* first = face.Find(l.uppercase ? 'D' : l.text[0]);
    ASSERT_NE(first, nullptr);
    const float baseline = (own.descent - own.ascent) * l.pixelSize / 2.0f;
    EXPECT_NEAR(q[0].yTop, baseline - first->oy * l.pixelSize, 1e-5f);
}

// label_3d.cpp breaks as Label does (BREAK_TRIM_EDGE_SPACES): a trailing
// newline adds no line, so a centred label does not move for it.
TEST(Label3D, ATrailingNewlineAddsNoLine) {
    assets::FontAtlas atlas;
    if (!Atlas(atlas)) GTEST_SKIP() << "no font atlas";
    const assets::Labels3D labels = Labels();
    assets::Label3D l = labels.All()[static_cast<std::size_t>(labels.Find(kShootDealer))];
    l.transparency = 0.0f;
    l.outlineModulate[3] = 0.0f;
    std::vector<assets::LabelQuad> plain, trailing;
    ASSERT_TRUE(assets::LayoutLabel3D(atlas, l, plain));
    l.text += "\n";
    ASSERT_TRUE(assets::LayoutLabel3D(atlas, l, trailing));
    ASSERT_EQ(plain.size(), trailing.size());
    ASSERT_FALSE(plain.empty());
    EXPECT_FLOAT_EQ(plain[0].yTop, trailing[0].yTop);
}

TEST(Label3D, TransparencyScalesAlpha) {
    assets::FontAtlas atlas;
    if (!Atlas(atlas)) GTEST_SKIP() << "no font atlas";
    const assets::Labels3D labels = Labels();
    assets::Label3D l = labels.All()[static_cast<std::size_t>(labels.Find(kShootDealer))];
    if (atlas.Find(assets::Label3DFaceKey(l)) < 0) GTEST_SKIP() << "face not exported";

    std::vector<assets::LabelQuad> q;
    ASSERT_TRUE(assets::LayoutLabel3D(atlas, l, q));
    for (const assets::LabelQuad& x : q) EXPECT_FLOAT_EQ(x.rgba[3], 0.0f);

    l.transparency = 0.25f;
    q.clear();
    ASSERT_TRUE(assets::LayoutLabel3D(atlas, l, q));
    for (const assets::LabelQuad& x : q) EXPECT_FLOAT_EQ(x.rgba[3], 0.75f);
}

TEST(Label3D, TheOutlinePassIsDrawnFirstWhenVisible) {
    // The health counter's DEALER keeps Label3D's default black outline.
    assets::FontAtlas atlas;
    if (!Atlas(atlas)) GTEST_SKIP() << "no font atlas";
    const assets::Labels3D labels = Labels();
    const int i = labels.Find(
        "tabletop parent/main tabletop/health counter/health counter ui parent/health UI_dealer side/text_dealer");
    ASSERT_GE(i, 0);
    const assets::Label3D& l = labels.All()[static_cast<std::size_t>(i)];
    if (atlas.Find(assets::Label3DOutlineFaceKey(l)) < 0) GTEST_SKIP() << "outline face not exported";

    std::vector<assets::LabelQuad> q;
    ASSERT_TRUE(assets::LayoutLabel3D(atlas, l, q));
    ASSERT_FALSE(q.empty());
    std::size_t outlines = 0;
    while (outlines < q.size() && q[outlines].outline) ++outlines;
    EXPECT_GT(outlines, 0u);
    EXPECT_EQ(outlines * 2, q.size()) << "outline and text passes should pair up";
    for (std::size_t k = outlines; k < q.size(); ++k) EXPECT_FALSE(q[k].outline);
}

TEST(Label3D, KnowsWhichAncestorsHideItAtLoad) {
    BSHT_NEEDS_FILE(kRoot + "labels3d.json");
    // The health counter's DEALER label is set visible but sits under parents
    // the counter only shows when it boots.
    const assets::Labels3D labels = Labels();
    const int i = labels.Find(
        "tabletop parent/main tabletop/health counter/health counter ui parent/health UI_dealer side/text_dealer");
    ASSERT_GE(i, 0);
    const assets::Label3D& l = labels.All()[static_cast<std::size_t>(i)];
    EXPECT_TRUE(l.visible);
    EXPECT_FALSE(l.hiddenAncestors.empty()) << "a hidden-at-load chain came through empty";

    const int s = labels.Find("ui parent_shooting decision/text dealer");
    ASSERT_GE(s, 0);
    EXPECT_TRUE(labels.All()[static_cast<std::size_t>(s)].hiddenAncestors.empty())
        << "the shooting choice is hidden by transparency, not by an ancestor";
}

TEST(Label3D, AppearsOnlyWhenEveryHiddenAncestorIsShown) {
    // The health counter's DEALER waits on HealthCounter.Bootup showing its panel.
    assets::Label3D l;
    l.path = "counter/panel/text_dealer";
    l.hiddenAncestors = {"counter/panel"};
    std::unordered_map<std::string, bool> ov;
    EXPECT_FALSE(assets::Label3DVisible(l, ov)) << "drew before the counter booted";
    ov["counter/panel"] = true;
    EXPECT_TRUE(assets::Label3DVisible(l, ov));
}

TEST(Label3D, HidingAnyAncestorHidesIt) {
    assets::Label3D l;
    l.path = "counter/panel/text_dealer";
    std::unordered_map<std::string, bool> ov;
    EXPECT_TRUE(assets::Label3DVisible(l, ov));
    ov["counter"] = false;   // HealthCounter.DisableCounter
    EXPECT_FALSE(assets::Label3DVisible(l, ov));
    ov["counter/pan"] = false;   // a sibling whose name is a prefix is not an ancestor
    ov["counter"] = true;
    EXPECT_TRUE(assets::Label3DVisible(l, ov));
}

TEST(Label3D, ItsOwnFlagAndOverrideStillCount) {
    assets::Label3D l;
    l.path = "a/b";
    l.visible = false;
    std::unordered_map<std::string, bool> ov;
    EXPECT_FALSE(assets::Label3DVisible(l, ov));
    ov["a/b"] = true;
    EXPECT_TRUE(assets::Label3DVisible(l, ov));
}

// Every Label3D a script writes the typed waiver name into must hold every
// letter a name can have: the faces carry only the characters export_fonts.gd
// was told about, and a missing one drops out of the name on the health counter.
TEST(Label3D, EveryLabelANameIsWrittenIntoHasTheWholeAlphabet) {
    assets::FontAtlas atlas;
    if (!Atlas(atlas)) GTEST_SKIP() << "no font atlas";
    const assets::Labels3D all = Labels();
    const std::pair<const char*, const char*> cases[] = {
        {"tabletop parent/main tabletop/health counter/health counter ui parent/health UI_player side/text_player",
         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
        {"tabletop parent/main tabletop/health counter/health counter ui parent/indicator_player win",
         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
        {"signature machine2/signature machine main parent/baseplate main/screen parent/letter_1",
         "abcdefghijklmnopqrstuvwxyz"},
        {"signature machine2/release form signed separate/letter_signed_separate_1",
         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
    };
    for (const auto& [path, alphabet] : cases) {
        const int i = all.Find(path);
        ASSERT_GE(i, 0) << path;
        assets::Label3D l = all.All()[static_cast<std::size_t>(i)];
        l.text = alphabet;
        std::vector<assets::LabelQuad> q;
        ASSERT_TRUE(assets::LayoutLabel3D(atlas, l, q)) << path;
        int glyphs = 0;
        for (const assets::LabelQuad& g : q) glyphs += g.outline ? 0 : 1;
        EXPECT_EQ(glyphs, 26) << path;
    }
}
