// The hardware probe's schedule and report (debug/hw_probe). The device run
// is one shot, so the schedule and the numbers it writes are pinned here.
#include "debug/hw_probe.h"

#include <gtest/gtest.h>

#include <string>

using debug::HwProbe;
using debug::ProbeVariant;

namespace {

debug::ProbeFrame Frame(double frameMs, int draws, double updateMs = 1.0, double drawMs = 2.0) {
    debug::ProbeFrame f;
    f.updateMs = updateMs;
    f.drawMs = drawMs;
    f.swapMs = frameMs - updateMs - drawMs;
    f.frameMs = frameMs;
    f.draws = draws;
    f.binds = draws / 2;
    f.tris = draws * 500;
    return f;
}

}  // namespace

TEST(HwProbe, PlaysNormallyUntilTheFirstFreeze) {
    HwProbe p({4.0f}, 2, 3);
    const HwProbe::Directive d = p.Next(1.0f);
    EXPECT_FALSE(d.freeze);
    EXPECT_TRUE(d.vsync);
    EXPECT_EQ(d.variant, ProbeVariant::Base);
}

TEST(HwProbe, AtAFreezeItRunsEveryVariantWithVsyncOffThenResumes) {
    HwProbe p({4.0f}, 2, 3);   // 2 warm-up frames, 3 measured frames per variant
    std::string order;
    int frozen = 0;
    for (int i = 0; i < 200; ++i) {
        const HwProbe::Directive d = p.Next(4.5f);
        if (!d.freeze) break;
        ++frozen;
        EXPECT_FALSE(d.vsync);
        order += static_cast<char>('0' + static_cast<int>(d.variant));
        p.Record(Frame(10.0, 100));
    }
    // 4 variants x (2 warm-up + 3 measured)
    EXPECT_EQ(frozen, 20);
    EXPECT_EQ(order, "00000111112222233333");
    EXPECT_TRUE(p.BenchDone());
    EXPECT_FALSE(p.Next(20.0f).freeze);
}

TEST(HwProbe, TheBenchReportsEachVariantsMeasuredFramesOnly) {
    HwProbe p({4.0f}, 1, 2);
    const double ms[4] = {10.0, 14.0, 7.0, 5.0};
    for (int i = 0; i < 12; ++i) {
        const HwProbe::Directive d = p.Next(4.0f);
        const int v = static_cast<int>(d.variant);
        const bool warm = (i % 3) == 0;
        p.Record(Frame(warm ? 99.0 : ms[v], 100));
    }
    const std::string out = p.FormatBench();
    EXPECT_NE(out.find("base 10.00"), std::string::npos) << out;
    EXPECT_NE(out.find("dyn 14.00"), std::string::npos) << out;
    EXPECT_NE(out.find("notex 7.00"), std::string::npos) << out;
    EXPECT_NE(out.find("batches 5.00"), std::string::npos) << out;
    EXPECT_EQ(out.find("99.00"), std::string::npos) << out;   // warm-up frames are not counted
}

TEST(HwProbe, PlayFramesAreBucketedByDrawCountWithTheirMisses) {
    HwProbe p({}, 1, 1);
    for (int i = 0; i < 10; ++i) p.Record(Frame(16.7, 120));
    for (int i = 0; i < 10; ++i) p.Record(Frame(i < 4 ? 33.3 : 16.7, 300));
    const std::string out = p.FormatPlay();
    EXPECT_NE(out.find("draws 100-199: 10 frames"), std::string::npos) << out;
    EXPECT_NE(out.find("draws 300-399: 10 frames"), std::string::npos) << out;
    EXPECT_NE(out.find("4 missed (40.0%)"), std::string::npos) << out;
}

TEST(HwProbe, TheSlowestPlayFramesAreKeptWithTheirSplit) {
    HwProbe p({}, 1, 1);
    for (int i = 0; i < 50; ++i) p.Record(Frame(16.7, 100));
    p.Record(Frame(40.0, 320, 9.0, 25.0));
    const std::string out = p.FormatPlay();
    EXPECT_NE(out.find("40.00 ms: update 9.00 draw 25.00 swap 6.00, 320 draws"), std::string::npos) << out;
}

// A player's build has no freeze times, so the probe only records play; its
// bench redraws without textures (white flashes).
TEST(HwProbe, WithoutFreezeTimesItNeverFreezesNorChangesTheFrame) {
    HwProbe p({}, 30, 90);
    for (float t = 0.0f; t < 60.0f; t += 0.5f) {
        const HwProbe::Directive d = p.Next(t);
        ASSERT_FALSE(d.freeze) << t;
        ASSERT_TRUE(d.vsync) << t;
        ASSERT_EQ(d.variant, ProbeVariant::Base) << t;
    }
    EXPECT_TRUE(p.BenchDone());
}
