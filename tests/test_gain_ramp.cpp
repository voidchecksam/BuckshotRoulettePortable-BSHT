// A stream voice's gain: Godot moves a playing sound's volume from the last
// mix buffer's value to the new one linearly across one 512-frame buffer
// (audio_server.cpp:638-648), so a fade or a moving 3D speaker never steps.
#include "core/gain_ramp.h"

#include <gtest/gtest.h>

TEST(GainRamp, AFreshRampHoldsItsGain) {
    core::GainRamp g;
    g.Reset(0.5f, 0.25f);
    float l = 0.0f, r = 0.0f;
    g.Next(l, r);
    EXPECT_FLOAT_EQ(l, 0.5f);
    EXPECT_FLOAT_EQ(r, 0.25f);
}

TEST(GainRamp, ANewTargetIsReachedLinearlyOverOneMixBuffer) {
    core::GainRamp g;
    g.Reset(0.0f, 1.0f);
    g.SetTarget(1.0f, 0.0f);
    float l = 0.0f, r = 0.0f;
    g.Next(l, r);   // frame 0: still the old gain
    EXPECT_FLOAT_EQ(l, 0.0f);
    EXPECT_FLOAT_EQ(r, 1.0f);
    for (int i = 1; i < 256; ++i) g.Next(l, r);
    g.Next(l, r);   // frame 256: half way
    EXPECT_NEAR(l, 0.5f, 1e-5f);
    EXPECT_NEAR(r, 0.5f, 1e-5f);
    for (int i = 257; i < 512; ++i) g.Next(l, r);
    g.Next(l, r);   // frame 512 on: the target
    EXPECT_FLOAT_EQ(l, 1.0f);
    EXPECT_FLOAT_EQ(r, 0.0f);
}

TEST(GainRamp, RetargetingMidRampStartsFromWhereItIs) {
    core::GainRamp g;
    g.Reset(0.0f, 0.0f);
    g.SetTarget(1.0f, 1.0f);
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i <= 256; ++i) g.Next(l, r);   // at 0.5
    g.SetTarget(0.0f, 0.0f);
    g.Next(l, r);
    EXPECT_NEAR(l, 0.5f, 2e-3f);
}
