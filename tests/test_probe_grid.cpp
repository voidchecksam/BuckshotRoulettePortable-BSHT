// The light probe grid and the moving-prop ratio.
//
// A prop's texels hold its colour at its bake pose. When a script or an
// animation carries it somewhere else, the CPU scales it by how much brighter
// the static light is where its pivot is now than where it was baked.
#include <gtest/gtest.h>

#include "assets/offstage.h"
#include "assets/probe_grid.h"

using assets::ProbeGrid;

namespace {

// 2 x 2 x 2 points, 0.5 apart from the origin; red = x index, green = y
// index, blue = z index, so trilinear results are easy to predict.
ProbeGrid Ramp() {
    ProbeGrid g;
    g.origin[0] = 1.0f;
    g.origin[1] = 2.0f;
    g.origin[2] = 3.0f;
    g.step = 0.5f;
    g.dims[0] = g.dims[1] = g.dims[2] = 2;
    g.rgb.resize(8 * 3);
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                float* p = &g.rgb[static_cast<std::size_t>(((z * 2 + y) * 2 + x) * 3)];
                p[0] = 1.0f + x;
                p[1] = 1.0f + y;
                p[2] = 1.0f + z;
            }
    return g;
}

}  // namespace

TEST(ProbeGrid, AGridPointReturnsItself) {
    const ProbeGrid g = Ramp();
    const float at[3] = {1.5f, 2.0f, 3.5f};   // x index 1, y 0, z 1
    float out[3];
    ASSERT_TRUE(g.Sample(at, out));
    EXPECT_FLOAT_EQ(out[0], 2.0f);
    EXPECT_FLOAT_EQ(out[1], 1.0f);
    EXPECT_FLOAT_EQ(out[2], 2.0f);
}

TEST(ProbeGrid, BetweenPointsItInterpolates) {
    const ProbeGrid g = Ramp();
    const float at[3] = {1.25f, 2.125f, 3.0f};
    float out[3];
    ASSERT_TRUE(g.Sample(at, out));
    EXPECT_FLOAT_EQ(out[0], 1.5f);
    EXPECT_FLOAT_EQ(out[1], 1.25f);
    EXPECT_FLOAT_EQ(out[2], 1.0f);
}

TEST(ProbeGrid, OutsideItClampsToTheEdge) {
    const ProbeGrid g = Ramp();
    const float at[3] = {-10.0f, 50.0f, 3.0f};
    float out[3];
    ASSERT_TRUE(g.Sample(at, out));
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    EXPECT_FLOAT_EQ(out[1], 2.0f);
}

TEST(ProbeGrid, AnEmptyGridHasNoSample) {
    const ProbeGrid g;
    const float at[3] = {0, 0, 0};
    float out[3];
    EXPECT_FALSE(g.Sample(at, out));
}

TEST(ProbeGrid, TheRatioIsOneAtTheBakePivot) {
    const ProbeGrid g = Ramp();
    const float pivot[3] = {1.2f, 2.3f, 3.1f};
    float r[3];
    assets::PropRatio(g, pivot, pivot, r);
    for (float c : r) EXPECT_FLOAT_EQ(c, 1.0f);
}

TEST(ProbeGrid, MovedUnderABrighterSpotTheRatioRises) {
    const ProbeGrid g = Ramp();
    const float baked[3] = {1.0f, 2.0f, 3.0f}, now[3] = {1.5f, 2.0f, 3.0f};
    float r[3];
    assets::PropRatio(g, now, baked, r);
    EXPECT_FLOAT_EQ(r[0], 2.0f);   // red doubles along x
    EXPECT_FLOAT_EQ(r[1], 1.0f);
    EXPECT_FLOAT_EQ(r[2], 1.0f);
}

TEST(ProbeGrid, TheRatioIsClampedAndADarkBakeGivesOne) {
    ProbeGrid g = Ramp();
    g.rgb[0] = 0.0f;           // the (0,0,0) point's red: black where baked
    g.rgb[1] = 0.01f;          // its green: very dark
    const float baked[3] = {1.0f, 2.0f, 3.0f}, now[3] = {1.5f, 2.5f, 3.0f};
    float r[3];
    assets::PropRatio(g, now, baked, r);
    EXPECT_FLOAT_EQ(r[0], 1.0f);   // zero denominator: no information, no change
    EXPECT_FLOAT_EQ(r[1], 4.0f);   // 2 / 0.01, clamped to 4
}

TEST(ProbeGrid, NoGridMeansNoChange) {
    const ProbeGrid g;
    const float a[3] = {0, 0, 0}, b[3] = {5, 5, 5};
    float r[3];
    assets::PropRatio(g, a, b, r);
    for (float c : r) EXPECT_FLOAT_EQ(c, 1.0f);
}

// Offstage actors: "dealer fly away" throws the dealer out of the lit room,
// where Godot draws him black. The probes cover only the lit part, so beyond
// them an offstage prop is dark instead of clamped to the grid's edge light.
TEST(ProbeGrid, ContainsIsTheGridsBox) {
    const ProbeGrid g = Ramp();   // x 1..1.5, y 2..2.5, z 3..3.5
    const float in[3] = {1.25f, 2.25f, 3.25f};
    const float edge[3] = {1.5f, 2.5f, 3.5f};
    const float out[3] = {9.0f, 2.25f, 3.25f};

    EXPECT_TRUE(g.Contains(in));
    EXPECT_TRUE(g.Contains(edge));
    EXPECT_FALSE(g.Contains(out));
    EXPECT_FALSE(ProbeGrid{}.Contains(in));
}

TEST(ProbeGrid, AnOffstagePropIsDarkBeyondTheProbes) {
    const ProbeGrid g = Ramp();
    const float bake[3] = {1.0f, 2.0f, 3.0f};
    const float away[3] = {60.0f, 2.0f, 3.0f};
    float ratio[3];

    assets::OffstageRatio(g, away, bake, ratio);

    EXPECT_FLOAT_EQ(ratio[0], 0.0f);
    EXPECT_FLOAT_EQ(ratio[1], 0.0f);
    EXPECT_FLOAT_EQ(ratio[2], 0.0f);
}

// Its texels are baked at the table, the brightest place it goes: leaning in
// toward the lamp (the probe light at its pivot doubles) must not light the
// face Godot keeps dark (frames 660-690 of shoot_dealer_b1r1).
TEST(ProbeGrid, InsideTheProbesAnOffstagePropIsNeverBrighterThanAtTheTable) {
    const ProbeGrid g = Ramp();
    const float bake[3] = {1.0f, 2.0f, 3.0f};
    const float brighter[3] = {1.5f, 2.0f, 3.0f};   // red 2 / 1
    float ratio[3];

    assets::OffstageRatio(g, brighter, bake, ratio);

    EXPECT_FLOAT_EQ(ratio[0], 1.0f);
    EXPECT_FLOAT_EQ(ratio[1], 1.0f);
    EXPECT_FLOAT_EQ(ratio[2], 1.0f);
}

TEST(ProbeGrid, InsideTheProbesAnOffstagePropDimsAsTheLightFalls) {
    const ProbeGrid g = Ramp();
    const float bake[3] = {1.5f, 2.0f, 3.0f};
    const float darker[3] = {1.0f, 2.0f, 3.0f};   // red 1 / 2
    float ratio[3];

    assets::OffstageRatio(g, darker, bake, ratio);

    EXPECT_FLOAT_EQ(ratio[0], 0.5f);
    EXPECT_FLOAT_EQ(ratio[1], 1.0f);
    EXPECT_FLOAT_EQ(ratio[2], 1.0f);
}

TEST(OffstageScope, OnlyTheDealersModelLeavesTheRoom) {
    EXPECT_TRUE(assets::InOffstageScope("dealer model parent/dealer head rigged1/Armature/Skeleton3D/dealer head mesh"));
    EXPECT_FALSE(assets::InOffstageScope("dealer hands main1/dealer hands parent/beer can parent"));
    EXPECT_FALSE(assets::InOffstageScope("tabletop parent/shotgun main parent"));
}

TEST(ProbeGrid, WithoutProbesAnOffstagePropIsLeftAsBaked) {
    const float bake[3] = {1.0f, 2.0f, 3.0f};
    const float away[3] = {60.0f, 2.0f, 3.0f};
    float ratio[3];

    assets::OffstageRatio(ProbeGrid{}, away, bake, ratio);

    EXPECT_FLOAT_EQ(ratio[0], 1.0f);
    EXPECT_FLOAT_EQ(ratio[1], 1.0f);
    EXPECT_FLOAT_EQ(ratio[2], 1.0f);
}
