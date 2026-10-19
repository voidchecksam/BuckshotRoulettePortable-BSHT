// Sampling rules transcribed from Godot 4.1.4 animation.cpp. The complete
// dataset is checked against the engine by parity_anim_sample; these pin the
// rules that were wrong in the old sampler, so a regression fails without Godot.
#include "godot/animation.h"

#include "assets/animation.h"
#include "godot/math.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

assets::Animation Parse(const char* json) {
    assets::AnimationLibrary lib;
    EXPECT_TRUE(lib.LoadFromString(json));
    return lib.Players().empty() ? assets::Animation{} : lib.Players()[0].animations[0];
}

double Sample1(const assets::Animation& a, double t) {
    assets::Value v;
    EXPECT_TRUE(godot::value_track_interpolate(a, 0, t, v));
    return v.f[0];
}

}  // namespace

TEST(GodotAnimation, ContinuousBoolFlipsAtTheMidpointNotAtTheKey) {
    // interpolate_variant's default case: c < 0.5 ? a : b.
    const assets::Animation a = Parse(R"({"format":2,"players":[{"path":"p","root":".","autoplay":"","animations":[
      {"name":"a","length":1,"loop":0,"step":0,"tracks":[{"type":0,"path":"n:visible","interp":1,"update":0,"loop_wrap":true,
       "keys":[{"t":0,"tr":1,"k":"b","v":false},{"t":1,"tr":1,"k":"b","v":true}]}]}]}]})");
    EXPECT_EQ(Sample1(a, 0.49), 0.0);
    EXPECT_EQ(Sample1(a, 0.51), 1.0);
}

TEST(GodotAnimation, DiscreteTracksSampleNearest) {
    // value_track_interpolate forces NEAREST for a DISCRETE track.
    const assets::Animation a = Parse(R"({"format":2,"players":[{"path":"p","root":".","autoplay":"","animations":[
      {"name":"a","length":1,"loop":0,"step":0,"tracks":[{"type":0,"path":"n:transparency","interp":1,"update":1,"loop_wrap":true,
       "keys":[{"t":0,"tr":1,"k":"f","v":0},{"t":1,"tr":1,"k":"f","v":10}]}]}]}]})");
    EXPECT_EQ(Sample1(a, 0.9), 0.0);
}

TEST(GodotAnimation, KeyTransitionEasesAndZeroHolds) {
    // c = 0.5, transition 2 -> ease(0.5, 2) = 0.25.
    const assets::Animation eased = Parse(R"({"format":2,"players":[{"path":"p","root":".","autoplay":"","animations":[
      {"name":"a","length":1,"loop":0,"step":0,"tracks":[{"type":0,"path":"n:transparency","interp":1,"update":0,"loop_wrap":true,
       "keys":[{"t":0,"tr":2,"k":"f","v":0},{"t":1,"tr":1,"k":"f","v":1}]}]}]}]})");
    EXPECT_NEAR(Sample1(eased, 0.5), 0.25, 1e-6);
    const assets::Animation held = Parse(R"({"format":2,"players":[{"path":"p","root":".","autoplay":"","animations":[
      {"name":"a","length":1,"loop":0,"step":0,"tracks":[{"type":0,"path":"n:transparency","interp":1,"update":0,"loop_wrap":true,
       "keys":[{"t":0,"tr":0,"k":"f","v":0},{"t":1,"tr":1,"k":"f","v":1}]}]}]}]})");
    EXPECT_EQ(Sample1(held, 0.9), 0.0);
}

TEST(GodotAnimation, CubicIsBarryGoldmanNotSmoothstep) {
    // Keys pre=0, from=1, to=2, post=5 at relative times -1, 0, 1, 2; weight 0.5.
    // Symmetric data cannot tell the two apart (zero tangents make the spline a
    // smoothstep), so these keys are deliberately uneven. By hand, math_funcs.h:293:
    //   t  = 0.5
    //   a1 = lerp(0, 1, (0.5+1)/1)       = 1.5
    //   a2 = lerp(1, 2, 0.5)             = 1.5
    //   a3 = lerp(2, 5, (0.5-1)/(2-1))   = 0.5
    //   b1 = lerp(1.5, 1.5, 1.5/2)       = 1.5
    //   b2 = lerp(1.5, 0.5, 0.5/2)       = 1.25
    //   r  = lerp(1.5, 1.25, 0.5)        = 1.375
    // Cross-check, Hermite with Catmull-Rom tangents m0=(2-0)/2=1, m1=(5-1)/2=2:
    //   0.5*1 + 0.125*1 + 0.5*2 - 0.125*2 = 1.375. The old smoothstep gives 1.5.
    EXPECT_NEAR(godot::cubic_interpolate_in_time(1.0, 2.0, 0.0, 5.0, 0.5, 1.0, -1.0, 2.0),
                1.375, 1e-12);
}

TEST(GodotAnimation, CubicValueTrackUsesItsNeighbours) {
    // Same numbers through a real cubic Vector3 track, sampled between keys 1 and 2.
    const assets::Animation a = Parse(R"({"format":2,"players":[{"path":"p","root":".","autoplay":"","animations":[
      {"name":"a","length":3,"loop":0,"step":0,"tracks":[{"type":0,"path":"n:position","interp":2,"update":0,"loop_wrap":true,
       "keys":[{"t":0,"tr":1,"k":"v3","v":[0,0,0]},{"t":1,"tr":1,"k":"v3","v":[1,0,0]},
               {"t":2,"tr":1,"k":"v3","v":[2,0,0]},{"t":3,"tr":1,"k":"v3","v":[5,0,0]}]}]}]}]})");
    EXPECT_NEAR(Sample1(a, 1.5), 1.375, 1e-5);
}

TEST(GodotAnimation, KeysInRangeExcludeFromIncludeTo) {
    const assets::Animation a = Parse(R"({"format":2,"players":[{"path":"p","root":".","autoplay":"","animations":[
      {"name":"a","length":1,"loop":0,"step":0,"tracks":[{"type":5,"path":"m","interp":1,"loop_wrap":true,
       "keys":[{"t":0,"tr":1,"m":"a","a":[]},{"t":0.5,"tr":1,"m":"b","a":[]},{"t":1,"tr":1,"m":"c","a":[]}]}]}]}]})");
    std::vector<int> idx;
    godot::track_get_key_indices_in_range(a, 0, 0.5, 0.5, godot::LoopedFlag::None, idx);
    ASSERT_EQ(idx.size(), 1u);
    EXPECT_EQ(idx[0], 1);   // (0, 0.5]: the key at 0 is excluded, 0.5 included
    idx.clear();
    godot::track_get_key_indices_in_range(a, 0, 0.5, 0.0, godot::LoopedFlag::None, idx);
    EXPECT_TRUE(idx.empty());   // delta 0 fires nothing
    EXPECT_EQ(godot::track_find_key(a, 0, 0.0, godot::FindMode::Exact), 0);
    EXPECT_EQ(godot::track_find_key(a, 0, 0.3, godot::FindMode::Exact), -1);
    EXPECT_EQ(godot::track_find_key(a, 0, 0.3, godot::FindMode::Nearest), 0);
}

TEST(GodotAnimation, QuaternionToEulerMatchesGodotYXZ) {
    // 90 degrees about Y.
    const godot::Quaternion q{0.0f, 0.70710678f, 0.0f, 0.70710678f};
    const godot::Vector3 e = godot::quat_to_euler_yxz(q);
    EXPECT_NEAR(e.x, 0.0f, 1e-5f);
    EXPECT_NEAR(e.y, 1.5707963f, 1e-5f);
    EXPECT_NEAR(e.z, 0.0f, 1e-5f);
}
