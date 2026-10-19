// A stream voice resamples its file's rate to the device's and keeps its
// place across buffers. Re-reading each buffer from scratch would throw two
// source frames away every callback: a tick per 21 ms and a track that runs
// 0.2 % fast.
#include "core/stream_resampler.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

// The audio callback must never touch the heap. A counting operator new for
// this test binary: it counts only while a test asks it to, on the asking
// thread.
namespace {
thread_local bool g_countAllocations = false;
thread_local int g_allocations = 0;
}  // namespace

void* operator new(std::size_t n) {
    if (g_countAllocations) ++g_allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

// A mono source that counts 0, 1, 2, ... so the output says which frame it read.
struct Ramp {
    int next = 0;
    int total = 1 << 30;
    int Pull(int16_t* dst, int maxFrames) {
        int n = 0;
        for (; n < maxFrames && next < total; ++n) dst[n] = static_cast<int16_t>(next++ % 30000);
        return n;
    }
};

TEST(StreamResampler, ConsumesExactlyTheSourceFramesItPlays) {
    Ramp src;
    core::StreamResampler r;
    r.Reset(1, 44100.0 / 48000.0);
    std::vector<float> out(1024 * 2);
    int produced = 0;
    for (int block = 0; block < 200; ++block) {
        produced += r.Render(1024, [&](int16_t* d, int m) { return src.Pull(d, m); }, out.data());
    }
    const double expectedPos = produced * (44100.0 / 48000.0);
    EXPECT_NEAR(r.Position(), expectedPos, 1.0) << "no frames dropped between buffers";
    // The last sample is the ramp at that position (linear interpolation).
    const double last = out[(1024 - 1) * 2];
    EXPECT_NEAR(last, std::fmod(expectedPos - 44100.0 / 48000.0, 30000.0), 2.0);
}

TEST(StreamResampler, StereoKeepsItsChannels) {
    core::StreamResampler r;
    r.Reset(2, 1.0);
    int k = 0;
    auto pull = [&](int16_t* d, int m) {
        for (int i = 0; i < m; ++i) { d[i * 2] = 100; d[i * 2 + 1] = -100; ++k; }
        return m;
    };
    std::vector<float> out(64 * 2);
    ASSERT_EQ(r.Render(64, pull, out.data()), 64);
    EXPECT_FLOAT_EQ(out[10], 100.0f);
    EXPECT_FLOAT_EQ(out[11], -100.0f);
}

TEST(StreamResampler, AnEndedSourceProducesFewerFrames) {
    Ramp src;
    src.total = 100;
    core::StreamResampler r;
    r.Reset(1, 1.0);
    std::vector<float> out(256 * 2);
    const int n = r.Render(256, [&](int16_t* d, int m) { return src.Pull(d, m); }, out.data());
    EXPECT_LT(n, 256);
    EXPECT_GE(n, 98);
    EXPECT_EQ(r.Render(256, [&](int16_t* d, int m) { return src.Pull(d, m); }, out.data()), 0);
}

}  // namespace

// The device's pull captures a decoder, its channel count and the loop flag --
// more than std::function's inline buffer on ARM32, so every callback went to
// the heap once per live stream. Any capture size must render allocation-free.
TEST(StreamResampler, RendersWithoutTouchingTheHeap) {
    Ramp src;
    core::StreamResampler r;
    r.Reset(1, 44100.0 / 48000.0);
    std::vector<float> out(2 * 1024);
    void* bulky[3] = {&src, nullptr, nullptr};   // a capture no inline buffer holds
    auto pull = [bulky](int16_t* d, int m) { return static_cast<Ramp*>(bulky[0])->Pull(d, m); };
    r.Render(1024, pull, out.data());   // warm: the chunk buffer grows once
    g_allocations = 0;
    g_countAllocations = true;
    for (int i = 0; i < 8; ++i) r.Render(1024, pull, out.data());
    g_countAllocations = false;
    EXPECT_EQ(g_allocations, 0);
}

// Music and ambience resample with Godot's cubic too (core::GodotCubic).
TEST(StreamResampler, ResamplesWithGodotsCubic) {
    struct Sine {
        int next = 0;
        int Pull(int16_t* dst, int maxFrames) {
            for (int n = 0; n < maxFrames; ++n, ++next) {
                dst[n] = static_cast<int16_t>(12000.0 * std::sin(2.0 * 3.14159265358979 * 10000.0 * next / 44100.0));
            }
            return maxFrames;
        }
    } src;
    core::StreamResampler r;
    r.Reset(1, 44100.0 / 48000.0);
    std::vector<float> out(2 * 8000);
    r.Render(8000, [&](int16_t* d, int m) { return src.Pull(d, m); }, out.data());
    double e = 0.0;
    for (int i = 1000; i < 7000; ++i) e += static_cast<double>(out[2 * i]) * out[2 * i];
    const double db = 10.0 * std::log10(e / 6000.0 / (12000.0 * 12000.0 / 2.0));
    EXPECT_NEAR(db, -0.37, 0.1) << "10 kHz came out " << db << " dB (linear gives -1.46)";
}
