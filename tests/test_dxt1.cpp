// DXT1 block decoding (the surface pages' format: the bake's inspection path
// decodes what stb_dxt encoded) and the frame-time distribution the reports
// print.
#include "assets/dxt1.h"
#include "debug/frame_timing.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

TEST(Dxt1, SolidBlockDecodesToTheNearest565Colour) {
    const assets::Dxt1Block b = assets::EncodeDxt1Solid(200, 100, 50);
    uint8_t rgba[16 * 4];
    assets::DecodeDxt1Block(b.data(), rgba);
    // 5:6:5 expanded by bit replication: 200 -> 24 -> 198 (25 would be 206,
    // farther), 100 -> 25 -> 101, 50 -> 6 -> 49.
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(rgba[i * 4 + 0], 198) << i;
        EXPECT_EQ(rgba[i * 4 + 1], 101) << i;
        EXPECT_EQ(rgba[i * 4 + 2], 49) << i;
        EXPECT_EQ(rgba[i * 4 + 3], 255) << i;
    }
}

TEST(Dxt1, FourColourModeInterpolatesThirds) {
    // c0 = white, c1 = black, c0 > c1 so four-colour mode; index 2 is 2/3 c0.
    const uint8_t block[8] = {0xFF, 0xFF, 0x00, 0x00, 0b10101010, 0, 0, 0};
    uint8_t rgba[16 * 4];
    assets::DecodeDxt1Block(block, rgba);
    EXPECT_EQ(rgba[0], 170);            // (2*255 + 0) / 3
    EXPECT_EQ(rgba[4 * 4], 255);        // second row, index 0
}

TEST(FrameTiming, ReportsMeanWorstPercentileAndMisses) {
    debug::FrameTiming t;
    for (int i = 0; i < 98; ++i) t.Add(10.0);
    t.Add(20.0);
    t.Add(40.0);
    EXPECT_EQ(t.Frames(), 100u);
    EXPECT_NEAR(t.MeanMs(), (98 * 10.0 + 60.0) / 100.0, 1e-9);
    EXPECT_EQ(t.WorstMs(), 40.0);
    EXPECT_EQ(t.PercentileMs(0.99), 20.0);   // the 99th of 100 sorted samples
    EXPECT_EQ(t.CountOver(16.9), 2u);
}

TEST(FrameTiming, EmptyIsZeroNotNan) {
    const debug::FrameTiming t;
    EXPECT_EQ(t.MeanMs(), 0.0);
    EXPECT_EQ(t.PercentileMs(0.99), 0.0);
}

}  // namespace
