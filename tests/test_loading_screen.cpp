// The loading screen's layout half, which links no GL (the same split as
// debug/overlay). A screen that lays out to nothing looks identical on a black
// frame to a font that failed to load, so the arithmetic is tested here.
#include "app/loading_screen.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

// The real exported atlas, metrics only: the layout does not need the 2 MB of
// coverage values and the test must not depend on them.
const char* kFontsJson = BSHT_SOURCE_DIR "/assets/extracted/ui_fonts.json";

const char* kTitle = "Buckshot Roulette";

}  // namespace

TEST(LoadingScreen, LaysOutTheTitleAndTheStageLine) {
    BSHT_NEEDS_FILE(kFontsJson);
    app::LoadingScreen s;
    ASSERT_TRUE(s.LoadFontMetricsForTest(kFontsJson));
    ASSERT_TRUE(s.FontReady());
    s.SetTitle(kTitle);
    s.SetStage("room");

    ASSERT_EQ(s.Items().size(), 2u);
    EXPECT_FALSE(s.Items()[0].quads.empty());   // the title
    EXPECT_FALSE(s.Items()[1].quads.empty());   // the stage line
}

TEST(LoadingScreen, TheStageLineChangesWithSetStage) {
    BSHT_NEEDS_FILE(kFontsJson);
    app::LoadingScreen s;
    ASSERT_TRUE(s.LoadFontMetricsForTest(kFontsJson));
    s.SetTitle(kTitle);

    s.SetStage("room");
    EXPECT_EQ(s.Stage(), "room");
    const std::size_t shortLine = s.Items().back().quads.size();

    s.SetStage("textures");
    EXPECT_EQ(s.Stage(), "textures");
    // "textures" has more inked glyphs than "room": the line really was rebuilt.
    EXPECT_GT(s.Items().back().quads.size(), shortLine);
}

TEST(LoadingScreen, TheTitleIsCentredInTheDesignWidth) {
    BSHT_NEEDS_FILE(kFontsJson);
    app::LoadingScreen s;
    ASSERT_TRUE(s.LoadFontMetricsForTest(kFontsJson));
    s.SetTitle(kTitle);
    s.SetStage("room");

    const std::vector<assets::TextQuad>& q = s.Items()[0].quads;
    ASSERT_FALSE(q.empty());
    float lo = q.front().x;
    float hi = q.front().x + q.front().w;
    for (const assets::TextQuad& g : q) {
        if (g.x < lo) lo = g.x;
        if (g.x + g.w > hi) hi = g.x + g.w;
    }
    EXPECT_NEAR((lo + hi) * 0.5f, app::LoadingScreen::kDesignWidth * 0.5f, 6.0f);
}

TEST(LoadingScreen, AMissingAtlasDrawsNothingRatherThanCrashing) {
    app::LoadingScreen s;
    EXPECT_FALSE(s.LoadFontMetricsForTest(BSHT_SOURCE_DIR "/assets/extracted/does_not_exist.json"));
    EXPECT_FALSE(s.FontReady());
    s.SetTitle(kTitle);
    s.SetStage("room");
    EXPECT_TRUE(s.Items().empty());
}

// The loading bar: a frame and a fill, laid out here so the drawing half only
// submits rectangles.
TEST(LoadingScreen, TheBarFillsInProportionToProgress) {
    app::LoadingScreen s;
    s.SetProgress(0.25f);
    const app::LoadingScreen::BarLayout b = s.Bar();
    EXPECT_GT(b.w, 0.0f);
    EXPECT_NEAR(b.fillW, 0.25f * (b.w - 2.0f * b.border), 1e-4f);
    // Centred in the design width.
    EXPECT_NEAR(b.x + b.w / 2.0f, app::LoadingScreen::kDesignWidth / 2.0f, 1e-3f);
}

TEST(LoadingScreen, ProgressIsClampedAndNeverRunsBackwards) {
    app::LoadingScreen s;
    s.SetProgress(0.6f);
    s.SetProgress(0.3f);   // a later stage with a lower estimate
    EXPECT_FLOAT_EQ(s.Progress(), 0.6f);
    s.SetProgress(4.0f);
    EXPECT_FLOAT_EQ(s.Progress(), 1.0f);
    app::LoadingScreen t;
    t.SetProgress(-1.0f);
    EXPECT_FLOAT_EQ(t.Progress(), 0.0f);
}

// The fonts span several pages, so the screen uploads every page its faces
// use, and every quad's page is among them. Drawn from page 0 alone, glyphs on
// another page sample garbage.
TEST(LoadingScreen, EveryQuadsPageIsAPageTheScreenUploads) {
    BSHT_NEEDS_FILE(kFontsJson);
    app::LoadingScreen s;
    ASSERT_TRUE(s.LoadFontMetricsForTest(kFontsJson));
    s.SetTitle(kTitle);
    s.SetStage("textures");
    const std::vector<int> pages = s.PagesNeeded();
    ASSERT_FALSE(pages.empty());
    for (const auto& item : s.Items()) {
        for (const auto& q : item.quads) {
            EXPECT_NE(std::find(pages.begin(), pages.end(), q.page), pages.end()) << "page " << q.page;
        }
    }
}

// A scene change draws with GameApp's resident atlas instead of reading (and,
// in the drawing half, uploading) a second copy.
TEST(LoadingScreen, ABorrowedAtlasLaysOutLikeALoadedOne) {
    BSHT_NEEDS_FILE(kFontsJson);
    assets::FontAtlas resident;
    ASSERT_EQ(resident.Load(kFontsJson, nullptr), assets::FontError::None);
    app::LoadingScreen loaded;
    ASSERT_TRUE(loaded.LoadFontMetricsForTest(kFontsJson));
    app::LoadingScreen borrowing;
    ASSERT_TRUE(borrowing.UseFonts(resident));
    for (app::LoadingScreen* s : {&loaded, &borrowing}) {
        s->SetTitle(kTitle);
        s->SetStage("room");
    }
    ASSERT_EQ(borrowing.Items().size(), loaded.Items().size());
    for (std::size_t i = 0; i < loaded.Items().size(); ++i) {
        ASSERT_EQ(borrowing.Items()[i].quads.size(), loaded.Items()[i].quads.size());
        for (std::size_t q = 0; q < loaded.Items()[i].quads.size(); ++q) {
            EXPECT_EQ(borrowing.Items()[i].quads[q].page, loaded.Items()[i].quads[q].page);
            EXPECT_FLOAT_EQ(borrowing.Items()[i].quads[q].x, loaded.Items()[i].quads[q].x);
        }
    }
    EXPECT_EQ(&borrowing.Fonts(), &resident) << "the screen reads the resident atlas, not a copy";
    EXPECT_EQ(borrowing.PagesNeeded(), loaded.PagesNeeded());
}
