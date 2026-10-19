// Vertex-colour light baking.
//
// Pure maths, so every rule is pinned directly rather than eyeballed on screen.
// The last test runs the real 29 lights over real extracted geometry, which is
// the case where a wrong falloff curve or a mis-transformed normal shows up as
// a uniformly black or uniformly white room.
#include "test_data.h"

#include <gtest/gtest.h>

#include "assets/bake.h"

#include <cmath>
#include <string>

using namespace assets;

namespace {

// A single vertex at the origin with a normal pointing along +Y.
Mesh OneVertexFacingUp() {
    Mesh m;
    m.positions = {0.0f, 0.0f, 0.0f};
    m.normals   = {0.0f, 1.0f, 0.0f};
    m.indices   = {0, 0, 0};
    return m;
}

BakedLight LightAt(float x, float y, float z, float range = 10.0f,
                   float energy = 1.0f) {
    BakedLight l;
    l.position[0] = x;
    l.position[1] = y;
    l.position[2] = z;
    l.range = range;
    l.energy = energy;
    l.colour[0] = l.colour[1] = l.colour[2] = 1.0f;
    return l;
}

// Exposure is pinned to 1.0 here: these tests measure the lighting maths
// (attenuation, Lambert, summing, tint), and a change to the default exposure
// must not silently rewrite what they assert.
BakeSettings NoAmbient() {
    BakeSettings s;
    s.ambient[0] = s.ambient[1] = s.ambient[2] = 0.0f;
    s.exposure = 1.0f;
    return s;
}

}  // namespace

// ------------------------------------------------------------ the falloff curve

TEST(Bake, AttenuationIsFullAtZeroDistanceAndZeroAtRange) {
    EXPECT_FLOAT_EQ(OmniAttenuation(0.0f, 10.0f), 1.0f);
    EXPECT_FLOAT_EQ(OmniAttenuation(10.0f, 10.0f), 0.0f);
    EXPECT_FLOAT_EQ(OmniAttenuation(11.0f, 10.0f), 0.0f);   // hard cut past range
}

TEST(Bake, AttenuationDecreasesMonotonically) {
    float prev = 2.0f;
    for (int i = 0; i <= 20; ++i) {
        const float d = static_cast<float>(i) * 0.5f;
        const float a = OmniAttenuation(d, 10.0f);
        EXPECT_LE(a, prev) << "attenuation rose at distance " << d;
        EXPECT_GE(a, 0.0f);
        EXPECT_LE(a, 1.0f);
        prev = a;
    }
}

TEST(Bake, ZeroOrNegativeRangeContributesNothing) {
    EXPECT_FLOAT_EQ(OmniAttenuation(1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(OmniAttenuation(1.0f, -5.0f), 0.0f);
}

// ------------------------------------------------------------------ the bake

TEST(Bake, AFullyFacingSurfaceGetsHALFEnergyUnderLambertWrap) {
    // Lambert wrap at roughness 1 (the spatial default,
    // scene_forward_clustered.glsl:754) gives
    //     max(0, (N.L + 1) / 4) = 0.5
    // facing the lamp, where plain max(N.L, 0) gives 1.0: twice too bright on
    // every surface facing a lamp.
    Mesh m = OneVertexFacingUp();
    // Directly above, so the +Y normal faces it: N.L = 1.
    const std::vector<BakedLight> lights = {LightAt(0.0f, 0.001f, 0.0f)};
    BakeVertexLighting(m, core::Mat4{}, lights, NoAmbient());

    ASSERT_EQ(m.colours.size(), 4u);
    EXPECT_NEAR(m.colours[0], 128, 1);
    EXPECT_EQ(m.colours[3], 255);      // alpha is always opaque
}

TEST(Bake, VertexBeyondRangeGetsAmbientOnly) {
    Mesh m = OneVertexFacingUp();
    const std::vector<BakedLight> lights = {LightAt(0.0f, 50.0f, 0.0f, /*range=*/10.0f)};
    BakeVertexLighting(m, core::Mat4{}, lights, NoAmbient());
    EXPECT_EQ(m.colours[0], 0);
    EXPECT_EQ(m.colours[1], 0);
    EXPECT_EQ(m.colours[2], 0);
}

TEST(Bake, AmbientIsAppliedWhenNoLightReaches) {
    Mesh m = OneVertexFacingUp();
    BakeSettings s;
    s.ambient[0] = 0.5f;
    s.ambient[1] = 0.0f;
    s.ambient[2] = 0.0f;
    s.exposure = 1.0f;
    BakeVertexLighting(m, core::Mat4{}, {}, s);
    EXPECT_NEAR(m.colours[0], 128, 1);
    EXPECT_EQ(m.colours[1], 0);
}

TEST(Bake, ASurfaceFacingAwayFromTheLightReceivesNothing) {
    // Normal points +Y; light is below at -Y. N.L is negative.
    Mesh m = OneVertexFacingUp();
    const std::vector<BakedLight> lights = {LightAt(0.0f, -2.0f, 0.0f)};
    BakeVertexLighting(m, core::Mat4{}, lights, NoAmbient());
    EXPECT_EQ(m.colours[0], 0);
}

TEST(Bake, AGrazingLightStillContributes) {
    // Replaces a test of the old useLambert escape hatch, which existed because
    // a strict Lambert term made lights inside their own fixtures self-shadow
    // into a black hole. diffuse_lambert_wrap solves that on its own: only a
    // surface facing DIRECTLY away receives nothing, and everything in between
    // carries up to a quarter of full brightness.
    Mesh m = OneVertexFacingUp();
    // Level with the vertex, so N.L is 0: the case plain Lambert blacks out.
    const std::vector<BakedLight> lights = {LightAt(2.0f, 0.0f, 0.0f)};
    BakeVertexLighting(m, core::Mat4{}, lights, NoAmbient());
    EXPECT_GT(m.colours[0], 0) << "a grazing light should still light the surface";
}

TEST(Bake, TwoLightsSum) {
    Mesh m = OneVertexFacingUp();
    const std::vector<BakedLight> one = {LightAt(0.0f, 5.0f, 0.0f, 10.0f, 0.3f)};
    BakeVertexLighting(m, core::Mat4{}, one, NoAmbient());
    const uint8_t single = m.colours[0];

    const std::vector<BakedLight> two = {LightAt(0.0f, 5.0f, 0.0f, 10.0f, 0.3f),
                                         LightAt(0.0f, 5.0f, 0.0f, 10.0f, 0.3f)};
    BakeVertexLighting(m, core::Mat4{}, two, NoAmbient());
    EXPECT_GT(m.colours[0], single) << "a second identical light added nothing";
}

TEST(Bake, OverBrightIsClampedNotWrapped) {
    // Without clamping, a float above 1 cast to uint8_t wraps toward black --
    // a lighting bug that appears in exactly the brightest places.
    Mesh m = OneVertexFacingUp();
    std::vector<BakedLight> lights;
    for (int i = 0; i < 40; ++i) lights.push_back(LightAt(0.0f, 0.5f, 0.0f, 10.0f, 5.0f));
    BakeVertexLighting(m, core::Mat4{}, lights, NoAmbient());
    EXPECT_EQ(m.colours[0], 255);
}

TEST(Bake, LightColourTintsTheResult) {
    Mesh m = OneVertexFacingUp();
    BakedLight red = LightAt(0.0f, 0.001f, 0.0f);
    red.colour[0] = 1.0f;
    red.colour[1] = 0.0f;
    red.colour[2] = 0.0f;
    BakeVertexLighting(m, core::Mat4{}, {red}, NoAmbient());
    // 128, not 255: lambert wrap gives 0.5 at full facing. What this test is
    // really about is that the RED channel carries and the others do not.
    EXPECT_NEAR(m.colours[0], 128, 1);
    EXPECT_EQ(m.colours[1], 0);
    EXPECT_EQ(m.colours[2], 0);
}

TEST(Bake, TheWorldTransformActuallyPlacesTheVertex) {
    // Vertex at local origin, mesh translated 100 units away from the light.
    // If the transform were ignored, the vertex would sit on the light and be
    // fully lit.
    Mesh m = OneVertexFacingUp();
    core::Mat4 world;
    world.m[12] = 100.0f;
    const std::vector<BakedLight> lights = {LightAt(0.0f, 0.001f, 0.0f, 10.0f)};
    BakeVertexLighting(m, world, lights, NoAmbient());
    EXPECT_EQ(m.colours[0], 0) << "world transform was not applied to the vertex";
}

TEST(Bake, HandlesAMeshWithNoVertices) {
    Mesh m;
    BakeVertexLighting(m, core::Mat4{}, {LightAt(0, 0, 0)}, NoAmbient());
    EXPECT_TRUE(m.colours.empty());
}

TEST(Bake, TheDecayTermMakesLightFallOffFasterWithDistance) {
    // OmniLight3D.attenuation is exported (the game's lights all use 1.0,
    // plain inverse distance). Without the decay term the falloff is far too
    // generous and the summed lights drive the room past white.
    const float geometricOnly = OmniAttenuation(5.0f, 10.0f, 0.0f);
    const float withDecay     = OmniAttenuation(5.0f, 10.0f, 1.0f);
    EXPECT_LT(withDecay, geometricOnly)
        << "the decay term is not reducing distant contribution";

    // Decay is inverse-distance: doubling the distance roughly halves the
    // extra factor, independent of the geometric term.
    const float near = OmniAttenuation(2.0f, 100.0f, 1.0f) /
                       OmniAttenuation(2.0f, 100.0f, 0.0f);
    const float far  = OmniAttenuation(4.0f, 100.0f, 1.0f) /
                       OmniAttenuation(4.0f, 100.0f, 0.0f);
    EXPECT_NEAR(near / far, 2.0f, 0.05f);
}

TEST(Bake, ExposureDefaultsToNeutralNowThatDecayIsReal) {
    // The default was 0.30 while the decay term was missing. That was a
    // correction for a missing input, not a look control; with the real term
    // exported it must be back to neutral or the compensation double-counts.
    BakeSettings def;
    EXPECT_FLOAT_EQ(def.exposure, 1.0f);
}

TEST(Bake, AVertexOnTopOfALightDoesNotProduceInfinity) {
    // pow(0, -1) is infinity. Godot clamps the distance to 0.0001 first, and
    // so does the port, or a single unlucky vertex turns into a spreading NaN.
    const float a = OmniAttenuation(0.0f, 10.0f, 1.0f);
    EXPECT_TRUE(std::isfinite(a));

    Mesh m = OneVertexFacingUp();
    std::vector<BakedLight> lights = {LightAt(0.0f, 0.0f, 0.0f)};
    lights[0].attenuation = 1.0f;
    BakeVertexLighting(m, core::Mat4{}, lights, NoAmbient());
    EXPECT_EQ(m.colours[3], 255);
    for (int c = 0; c < 3; ++c) {
        EXPECT_LE(m.colours[c], 255);
    }
}

TEST(Bake, ProducesFourBytesPerVertex) {
    Mesh m;
    m.positions = {0,0,0,  1,0,0,  2,0,0};
    m.normals   = {0,1,0,  0,1,0,  0,1,0};
    BakeVertexLighting(m, core::Mat4{}, {}, BakeSettings{});
    EXPECT_EQ(m.colours.size(), 12u);
    EXPECT_TRUE(m.HasColours());
}

// -------------------------------------------------- the real room's real lights

TEST(Bake, BakesRealGeometryWithTheRealTwentyNineLights) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/scene_graph.json");
    Scene scene;
    const std::string scenePath =
        std::string(BSHT_SOURCE_DIR) + "/assets/extracted/scene_graph.json";
    ASSERT_TRUE(scene.LoadFromFile(scenePath.c_str()));

    const std::vector<BakedLight> lights = CollectLights(scene);
    ASSERT_EQ(lights.size(), 29u);

    // Lights must not all be stacked at the origin: that is what a local/world
    // transform mix-up looks like, and it produces a plausible but wrong bake.
    float minX = 1e30f, maxX = -1e30f;
    for (const BakedLight& l : lights) {
        minX = std::min(minX, l.position[0]);
        maxX = std::max(maxX, l.position[0]);
        EXPECT_GT(l.range, 0.0f);
    }
    EXPECT_GT(maxX - minX, 1.0f) << "all 29 lights resolved to the same place";

    Model model;
    const std::string glb =
        std::string(BSHT_SOURCE_DIR) + "/assets/extracted/models/backroom_blockout6.glb";
    ASSERT_EQ(LoadGlb(glb.c_str(), model), GlbError::None);
    ASSERT_FALSE(model.meshes.empty());

    int litVertices = 0, totalVertices = 0;
    for (Mesh& mesh : model.meshes) {
        BakeVertexLighting(mesh, core::Mat4{}, lights);
        ASSERT_EQ(mesh.colours.size(), static_cast<std::size_t>(mesh.VertexCount()) * 4);
        for (int v = 0; v < mesh.VertexCount(); ++v) {
            const std::size_t c = static_cast<std::size_t>(v) * 4;
            EXPECT_EQ(mesh.colours[c + 3], 255);
            const int lum = mesh.colours[c] + mesh.colours[c + 1] + mesh.colours[c + 2];
            if (lum > 3 * 20) ++litVertices;    // brighter than the ambient floor
            ++totalVertices;
        }
    }
    EXPECT_GT(totalVertices, 0);
    // A room where nothing is lit means the falloff or the transform is wrong.
    EXPECT_GT(litVertices, 0) << "no vertex received any light";

    // The same geometry 1000 units away is past every light's range.
    int litFarAway = 0;
    for (Mesh& mesh : model.meshes) {
        BakeVertexLighting(mesh, core::Translation(1000.0f, 0.0f, 0.0f), lights);
        for (int v = 0; v < mesh.VertexCount(); ++v) {
            const std::size_t c = static_cast<std::size_t>(v) * 4;
            if (mesh.colours[c] + mesh.colours[c + 1] + mesh.colours[c + 2] > 3 * 20) ++litFarAway;
        }
    }
    EXPECT_EQ(litFarAway, 0) << "light reached 1000 units; falloff is broken";
}
