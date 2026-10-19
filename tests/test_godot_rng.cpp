// godot::RandomPCG must reproduce Godot 4.1's global RNG exactly: the dealer's
// coin flip, shell order and item draws all go through it, and parity_rng diffs
// it against the real engine call for call. These tests pin the generator
// against the published PCG32 reference; the Godot range mappings are pinned by
// parity_rng, which is the only authority on them.
#include "godot/rng.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

// pcg-c-basic's pcg32-demo: pcg32_srandom_r(&rng, 42u, 54u) prints these six.
TEST(GodotRng, Pcg32MatchesTheReferenceStream) {
    godot::RandomPCG r(42u, 54u);
    const uint32_t expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
                                 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (uint32_t e : expected) EXPECT_EQ(r.rand(), e);
}

TEST(GodotRng, ReseedingRestartsTheStream) {
    godot::RandomPCG r;
    r.seed(12345);
    const uint32_t a = r.rand();
    r.rand();
    r.seed(12345);
    EXPECT_EQ(r.rand(), a);
}

TEST(GodotRng, IntRangeIsInclusiveAndOrderFree) {
    godot::RandomPCG r;
    r.seed(7);
    bool sawLo = false, sawHi = false;
    for (int i = 0; i < 2000; ++i) {
        const int v = r.random(5, -5);   // Godot accepts reversed bounds
        ASSERT_GE(v, -5);
        ASSERT_LE(v, 5);
        sawLo |= v == -5;
        sawHi |= v == 5;
    }
    EXPECT_TRUE(sawLo);
    EXPECT_TRUE(sawHi);
    EXPECT_EQ(r.random(3, 3), 3);
}

TEST(GodotRng, FloatsStayInTheUnitInterval) {
    godot::RandomPCG r;
    r.seed(99);
    for (int i = 0; i < 2000; ++i) {
        const float f = r.randf();
        const double d = r.randd();
        ASSERT_GE(f, 0.0f);
        ASSERT_LE(f, 1.0f);
        ASSERT_GE(d, 0.0);
        ASSERT_LE(d, 1.0);
    }
}

// GDScript's randf() is Math::randf() (math_funcs.h:507 at 4.1.4): one draw
// divided by RANDOM_32BIT_MAX, not RandomPCG::randf(), which takes two.
// With two, every value after the first randf() drifts (parity_rng).
TEST(GodotRng, GdscriptRandfIsOneDrawOverUint32Max) {
    godot::RandomPCG reference;
    reference.seed(31337);
    const uint32_t draw = reference.rand();
    const uint32_t following = reference.rand();

    godot::seed(31337);
    const double f = godot::randf();
    EXPECT_DOUBLE_EQ(f, static_cast<double>(static_cast<float>(draw) /
                                            static_cast<float>(0xFFFFFFFFu)));
    EXPECT_EQ(godot::randi(), static_cast<int64_t>(following));
}

TEST(GodotRng, GlobalFunctionsShareOneGenerator) {
    godot::seed(2024);
    const int64_t a = godot::randi_range(0, 1000000);
    godot::seed(2024);
    EXPECT_EQ(godot::randi_range(0, 1000000), a);
}

// Godot's SceneTree constructor calls Math::randomize() (scene_tree.cpp:1696 at
// 4.1.4), seeding the global RNG from the clock, so each boot deals different
// shells, items and dealer choices. Scripts that want determinism call seed()
// afterwards, which is what the parity harness does.
TEST(GodotRng, ANewSceneTreeRandomizesTheGlobalGenerator) {
    godot::seed(godot::RandomPCG::kDefaultSeed);
    { godot::SceneTree a; }
    const uint64_t first = godot::default_rand().get_seed();
    EXPECT_NE(first, godot::RandomPCG::kDefaultSeed);
    { godot::SceneTree b; }
    EXPECT_NE(godot::default_rand().get_seed(), first) << "two boots must not share a seed";
}
