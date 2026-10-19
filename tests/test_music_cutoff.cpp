// app/music_cutoff: the Vita's speaker plays little below a few hundred Hz,
// and the original muffles the table's music with a 156 Hz low-pass, so
// batches 1-2 play no audible music there. The cutoff is scaled rather than
// dropped, so the shape stays: the table more muffled than batch 3, the lobby
// still open.
#include "app/music_cutoff.h"

#include <gtest/gtest.h>

namespace {

TEST(MusicCutoff, TheDesktopKeepsGodotsCutoff) {
    EXPECT_FLOAT_EQ(app::ScaledMusicCutoff(156.0f, 1.0f), 156.0f);
}

TEST(MusicCutoff, TheVitaRaisesItAndKeepsTheOrder) {
    const float table = app::ScaledMusicCutoff(156.0f, app::kVitaMusicCutoffScale);
    const float batch3 = app::ScaledMusicCutoff(436.0f, app::kVitaMusicCutoffScale);

    EXPECT_FLOAT_EQ(table, 780.0f);
    EXPECT_FLOAT_EQ(batch3, 2180.0f);
    EXPECT_LT(table, batch3);
}

TEST(MusicCutoff, NoFilterStaysNoFilter) {
    EXPECT_FLOAT_EQ(app::ScaledMusicCutoff(0.0f, app::kVitaMusicCutoffScale), 0.0f);
}

TEST(MusicCutoff, AScaledCutoffStaysBelowTheAudibleTop) {
    EXPECT_FLOAT_EQ(app::ScaledMusicCutoff(8000.0f, app::kVitaMusicCutoffScale), app::kMaxMusicCutoffHz);
}

}  // namespace
