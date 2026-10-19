// The per-system frame profiler. A fake clock drives it, so every number below
// is exact.
#include "core/profiler.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

uint64_t g_now = 0;
uint64_t FakeClock() { return g_now; }

using core::ProfSection;

TEST(Profiler, ScopesAccumulateIntoTheirSectionForTheFrame) {
    core::Profiler p(&FakeClock);
    g_now = 1000;
    {
        core::ProfScope update(&p, ProfSection::Update);
        g_now += 300;
        {
            core::ProfScope anim(&p, ProfSection::Animation);
            g_now += 200;
        }
        {
            core::ProfScope anim(&p, ProfSection::Animation);   // a second player
            g_now += 100;
        }
    }
    p.EndFrame();
    EXPECT_EQ(p.Frames(), 1);
    EXPECT_DOUBLE_EQ(p.MeanMs(ProfSection::Update), 0.6);
    EXPECT_DOUBLE_EQ(p.MeanMs(ProfSection::Animation), 0.3);
    EXPECT_DOUBLE_EQ(p.MaxMs(ProfSection::Animation), 0.3);
}

TEST(Profiler, TheWindowKeepsTheMeanAndTheWorstFrame) {
    core::Profiler p(&FakeClock);
    for (uint64_t cost : {1000u, 3000u, 2000u}) {
        g_now = 0;
        {
            core::ProfScope s(&p, ProfSection::Props);
            g_now += cost;
        }
        p.EndFrame();
    }
    EXPECT_EQ(p.Frames(), 3);
    EXPECT_DOUBLE_EQ(p.MeanMs(ProfSection::Props), 2.0);
    EXPECT_DOUBLE_EQ(p.MaxMs(ProfSection::Props), 3.0);
    p.ResetWindow();
    EXPECT_EQ(p.Frames(), 0);
    EXPECT_DOUBLE_EQ(p.MeanMs(ProfSection::Props), 0.0);
}

TEST(Profiler, ANullProfilerScopeIsANoOp) {
    core::ProfScope s(nullptr, ProfSection::Update);   // must not crash
    SUCCEED();
}

TEST(Profiler, FormatNamesEverySectionThatRan) {
    core::Profiler p(&FakeClock);
    g_now = 0;
    {
        core::ProfScope s(&p, ProfSection::Skinning);
        g_now += 1500;
    }
    p.EndFrame();
    const std::string line = p.Format();
    EXPECT_EQ(line.rfind("prof: 1 frames", 0), 0u) << line;
    EXPECT_NE(line.find("skin 1.50/1.50"), std::string::npos) << line;
    EXPECT_EQ(line.find("props"), std::string::npos) << "a section that never ran is left out: " << line;
}

uint32_t g_allocs = 0;
uint32_t FakeAllocs() { return g_allocs; }

TEST(Profiler, CountsAllocationsPerFrameFromItsCounter) {
    g_allocs = 1000;   // whatever happened before the counter was attached
    core::Profiler p(&FakeClock);
    p.SetAllocCounter(&FakeAllocs);
    g_allocs += 10;
    p.EndFrame();
    g_allocs += 30;
    p.EndFrame();
    EXPECT_DOUBLE_EQ(p.MeanAllocs(), 20.0);
    EXPECT_EQ(p.MaxAllocs(), 30u);
    EXPECT_NE(p.Format().find(" allocs 20.0/30"), std::string::npos) << p.Format();
    p.ResetWindow();
    g_allocs += 4;
    p.EndFrame();
    EXPECT_DOUBLE_EQ(p.MeanAllocs(), 4.0) << "the window restarts; the counter's base does not";
}

TEST(Profiler, TheCounterWrapsWithoutAJump) {
    g_allocs = 0xFFFFFFF0u;
    core::Profiler p(&FakeClock);
    p.SetAllocCounter(&FakeAllocs);
    g_allocs += 0x20u;   // wraps past zero
    p.EndFrame();
    EXPECT_EQ(p.MaxAllocs(), 0x20u);
}

TEST(Profiler, WithoutACounterAllocationsAreLeftOut) {
    core::Profiler p(&FakeClock);
    p.EndFrame();
    EXPECT_EQ(p.Format().find("allocs"), std::string::npos) << p.Format();
}

}  // namespace
