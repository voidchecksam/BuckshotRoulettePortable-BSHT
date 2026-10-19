// The light model, pinned to numbers computed by hand from Godot 4.1.4 source.
//
// Every constant below is traceable to a line of the engine the original was
// built in:
//
//   lambert wrap    scene_forward_lights_inc.glsl:119
//   roughness 1.0   scene_forward_clustered.glsl:754  (the spatial default)
//   attenuation     scene_forward_lights_inc.glsl:406
//   srgb_to_linear  core/math/color.h:175
//   energy *= PI    light_storage.cpp:842, which cancels the BRDF's 1/PI exactly
//
// A wrong light model is not a crash or a failing test elsewhere: it is a room
// that does not look like the game (2x overbright facing a lamp, hard black at
// grazing angles, light colours never linearised). A formula that drifts must
// fail here.
#include "assets/bake.h"

#include <gtest/gtest.h>

namespace {

// light_tabletop interior, verbatim from assets/extracted/scene_graph.json.
// The brightest light in the room and the one that paints the pool on the
// table, so it is the one whose numbers matter most.
assets::BakedLight Tabletop() {
    assets::BakedLight l;
    l.position[0] = 0.0f;
    l.position[1] = 0.0f;
    l.position[2] = 0.0f;
    l.colour[0] = assets::SrgbToLinear(0.86f);
    l.colour[1] = assets::SrgbToLinear(0.42f);
    l.colour[2] = assets::SrgbToLinear(0.12f);
    l.energy = 32.77f;
    l.range  = 16.90f;
    l.attenuation = 1.0f;
    return l;
}

}  // namespace

TEST(Lighting, SrgbToLinearMatchesGodot) {
    // core/math/color.h:175: linear below 0.04045, a 2.4 power above it.
    // Getting this wrong turns the original's deep amber into a pale
    // washed-out yellow.
    EXPECT_NEAR(assets::SrgbToLinear(0.0f),  0.0f,      1e-6f);
    EXPECT_NEAR(assets::SrgbToLinear(0.04f), 0.003096f, 1e-5f);
    EXPECT_NEAR(assets::SrgbToLinear(0.12f), 0.013412f, 1e-5f);
    EXPECT_NEAR(assets::SrgbToLinear(0.42f), 0.147319f, 1e-5f);
    EXPECT_NEAR(assets::SrgbToLinear(0.86f), 0.710566f, 1e-5f);
    EXPECT_NEAR(assets::SrgbToLinear(1.0f),  1.0f,      1e-6f);
}

TEST(Lighting, LambertWrapIsNotPlainLambert) {
    // The whole point of diffuse_lambert_wrap: at grazing and back-facing
    // angles the original still carries light where plain max(N.L, 0) goes
    // hard black. That black is 29.6% of the old bake's vertices.
    EXPECT_NEAR(assets::LambertWrap( 1.0f), 0.500f, 1e-6f);
    EXPECT_NEAR(assets::LambertWrap( 0.5f), 0.375f, 1e-6f);
    EXPECT_NEAR(assets::LambertWrap( 0.0f), 0.250f, 1e-6f);   // was 0.0
    EXPECT_NEAR(assets::LambertWrap(-0.5f), 0.125f, 1e-6f);   // was 0.0
    EXPECT_NEAR(assets::LambertWrap(-1.0f), 0.000f, 1e-6f);
}

TEST(Lighting, LambertWrapClampsNdotLAboveOne) {
    // Godot: min(A + dot(N, L), 1.0). A is size_A, which is 0 for an
    // OmniLight3D whose size is the default 0. An unnormalised normal must not
    // be able to push a surface past full brightness.
    EXPECT_NEAR(assets::LambertWrap(1.5f), 0.5f, 1e-6f);
}

TEST(Lighting, AttenuationStillMatchesGodot) {
    // Godot's own falloff, pinned so a refactor cannot quietly alter it.
    EXPECT_NEAR(assets::OmniAttenuation(5.0f, 16.90f, 1.0f), 0.196947f, 1e-5f);
    EXPECT_NEAR(assets::OmniAttenuation(16.90f, 16.90f, 1.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(assets::OmniAttenuation(20.0f, 16.90f, 1.0f), 0.0f, 1e-6f);
}

TEST(Lighting, ContributionMatchesHandComputedGodotValues) {
    const assets::BakedLight l = Tabletop();
    const float pos[3] = {5.0f, 0.0f, 0.0f};   // 5 units from the light
    float rgb[3] = {0.0f, 0.0f, 0.0f};

    // The vertex is at +5 on X and the light is at the origin, so the normal
    // that FACES the light points in -X.
    const float facing[3] = {-1.0f, 0.0f, 0.0f};
    assets::LightContribution(l, pos, facing, rgb);
    EXPECT_NEAR(rgb[0], 2.292981f, 1e-4f);
    EXPECT_NEAR(rgb[1], 0.475395f, 1e-4f);
    EXPECT_NEAR(rgb[2], 0.043279f, 1e-4f);

    // Grazing: N.L = 0. Exactly HALF the facing value, not zero.
    const float grazing[3] = {0.0f, 1.0f, 0.0f};
    assets::LightContribution(l, pos, grazing, rgb);
    EXPECT_NEAR(rgb[0], 1.146491f, 1e-4f);
    EXPECT_NEAR(rgb[1], 0.237697f, 1e-4f);
    EXPECT_NEAR(rgb[2], 0.021640f, 1e-4f);

    // Facing directly away: N.L = -1. Zero, as in Godot.
    const float away[3] = {1.0f, 0.0f, 0.0f};
    assets::LightContribution(l, pos, away, rgb);
    EXPECT_NEAR(rgb[0], 0.0f, 1e-6f);
    EXPECT_NEAR(rgb[1], 0.0f, 1e-6f);
    EXPECT_NEAR(rgb[2], 0.0f, 1e-6f);
}

TEST(Lighting, ContributionIsZeroBeyondRange) {
    // Godot's falloff cuts hard at range. A light must not reach across the
    // room just because the surface faces it.
    const assets::BakedLight l = Tabletop();
    const float far[3] = {40.0f, 0.0f, 0.0f};
    const float facing[3] = {1.0f, 0.0f, 0.0f};
    float rgb[3] = {1.0f, 1.0f, 1.0f};
    assets::LightContribution(l, far, facing, rgb);
    EXPECT_FLOAT_EQ(rgb[0], 0.0f);
    EXPECT_FLOAT_EQ(rgb[1], 0.0f);
    EXPECT_FLOAT_EQ(rgb[2], 0.0f);
}

TEST(Lighting, TheTableLightIsDeepAmberNotPaleYellow) {
    // The most visible difference from the original when it is wrong: in
    // linear space the table light's blue is 0.013, an order of magnitude
    // under its green. Summing the raw sRGB values instead renders it 9.2x too
    // blue.
    const assets::BakedLight l = Tabletop();
    EXPECT_LT(l.colour[2], l.colour[1] * 0.15f) << "the amber has gone yellow";
    EXPECT_GT(l.colour[0], 0.7f);
    EXPECT_NEAR(l.colour[1] / l.colour[2], 10.98f, 0.2f)
        << "green:blue ratio no longer matches Godot's linearised colour";
}
