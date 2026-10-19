// The mixer's one clamp: the 32-bit bus at the master gain to 16 bits,
// counting the samples that clipped, so the device log can name an audible
// distortion.
#include "core/bus_clamp.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

TEST(BusClamp, ScalesClampsAndCountsTheClippedSamples) {
    const std::vector<int32_t> bus{100, -100, 40000, -40000, 32767, -32768};
    std::vector<int16_t> out(bus.size(), 0);
    const int clipped = core::ClampBus(bus.data(), out.data(), static_cast<int>(bus.size()), 1.0f);
    EXPECT_EQ(clipped, 2);
    EXPECT_EQ(out[0], 100);
    EXPECT_EQ(out[1], -100);
    EXPECT_EQ(out[2], 32767);
    EXPECT_EQ(out[3], -32768);
    EXPECT_EQ(out[4], 32767);
    EXPECT_EQ(out[5], -32768);
}

TEST(BusClamp, MasterGainAppliesBeforeTheClamp) {
    const std::vector<int32_t> bus{40000, 20000};
    std::vector<int16_t> out(bus.size(), 0);
    EXPECT_EQ(core::ClampBus(bus.data(), out.data(), 2, 0.5f), 0);
    EXPECT_EQ(out[0], 20000);
    EXPECT_EQ(out[1], 10000);
}
