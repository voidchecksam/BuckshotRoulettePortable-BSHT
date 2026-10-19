// The surface bake: shadows and the colour of one texel. Expected values are
// composed from the pinned light_model functions, so these tests check the
// wiring (which light, which normal, which order) rather than re-deriving
// Godot's maths.
#include "assets/light_bvh.h"
#include "assets/light_model.h"
#include "assets/surface_bake.h"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace assets;

namespace {

// Two triangles: the axis-aligned square [x0,x1] x [z0,z1] at height y.
void AddQuadY(std::vector<float>& tris, float x0, float x1, float z0, float z1, float y) {
    const float q[18] = {x0, y, z0, x1, y, z0, x1, y, z1,
                         x0, y, z0, x1, y, z1, x0, y, z1};
    tris.insert(tris.end(), q, q + 18);
}

BakedLight WhiteLightAbove(bool shadow, float size = 0.0f) {
    BakedLight l;
    l.position[0] = 0.0f; l.position[1] = 2.0f; l.position[2] = 0.0f;
    l.colour[0] = l.colour[1] = l.colour[2] = 1.0f;   // linear
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    l.shadow = shadow;
    l.size = size;
    return l;
}

const float kPos[3] = {0.0f, 0.0f, 0.0f};
const float kUp[3] = {0.0f, 1.0f, 0.0f};
const float kEyeAbove[3] = {0.0f, 5.0f, 0.0f};

}  // namespace

TEST(LightBvh, OccludedMatchesBruteForce) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-5.0f, 5.0f);
    // Small triangles scattered through the cube, so rays both hit and miss.
    std::uniform_real_distribution<float> small(-0.6f, 0.6f);
    std::vector<float> tris;
    for (int i = 0; i < 300; ++i) {
        const float c[3] = {u(rng), u(rng), u(rng)};
        for (int k = 0; k < 9; ++k) tris.push_back(c[k % 3] + small(rng));
    }
    LightBvh bvh;
    bvh.Build(tris);
    ASSERT_EQ(bvh.TriangleCount(), 300u);
    LightBvh brute;
    brute.BuildFlat(tris);   // no hierarchy: every ray tests every triangle
    int hits = 0;
    for (int r = 0; r < 500; ++r) {
        const float a[3] = {u(rng), u(rng), u(rng)};
        const float b[3] = {u(rng), u(rng), u(rng)};
        const bool o = bvh.Occluded(a, b);
        EXPECT_EQ(o, brute.Occluded(a, b)) << "ray " << r;
        hits += o;
    }
    EXPECT_GT(hits, 50);    // the test means something only if some rays hit
    EXPECT_LT(hits, 450);
}

TEST(LightBvh, AHitBeyondTheSegmentDoesNotOcclude) {
    std::vector<float> tris;
    AddQuadY(tris, -1, 1, -1, 1, 3.0f);   // above the light
    LightBvh bvh;
    bvh.Build(tris);
    const float from[3] = {0, 0, 0}, to[3] = {0, 2, 0};
    EXPECT_FALSE(bvh.Occluded(from, to));
}

TEST(SurfaceBake, StaticLightUnderOneLampIsGodotsValue) {
    LightBvh empty;
    empty.Build({});
    float out[3];
    StaticLight({WhiteLightAbove(true)}, empty, kPos, kUp, kEyeAbove, 1.0f, out);
    // colour 1 * energy 2 * attenuation(2, 10, 1) * Burley facing (1)
    for (float v : out) EXPECT_NEAR(v, 2.0f * 0.49840128f, 1e-5f);
}

TEST(SurfaceBake, ShadowEnabledLightIsBlockedAndOthersAreNot) {
    std::vector<float> tris;
    AddQuadY(tris, -1, 1, -1, 1, 1.0f);   // between the lamp and the point
    LightBvh bvh;
    bvh.Build(tris);
    float out[3];
    StaticLight({WhiteLightAbove(true)}, bvh, kPos, kUp, kEyeAbove, 1.0f, out);
    for (float v : out) EXPECT_EQ(v, 0.0f);
    StaticLight({WhiteLightAbove(false)}, bvh, kPos, kUp, kEyeAbove, 1.0f, out);
    for (float v : out) EXPECT_NEAR(v, 2.0f * 0.49840128f, 1e-5f);
}

TEST(SurfaceBake, ASizedLightIsHalfBlockedByAnEdge) {
    std::vector<float> tris;
    AddQuadY(tris, -5, 0, -5, 5, 1.0f);   // covers only x < 0
    LightBvh bvh;
    bvh.Build(tris);
    LightBvh empty;
    empty.Build({});
    float open[3], half[3];
    StaticLight({WhiteLightAbove(true, 1.0f)}, empty, kPos, kUp, kEyeAbove, 1.0f, open);
    StaticLight({WhiteLightAbove(true, 1.0f)}, bvh, kPos, kUp, kEyeAbove, 1.0f, half);
    EXPECT_GT(half[0], 0.2f * open[0]);
    EXPECT_LT(half[0], 0.8f * open[0]);
}

TEST(SurfaceBake, TexelColourIsGodotsChain) {
    const float texel[3] = {0.5f, 0.5f, 0.5f};
    const float light[3] = {0.99680256f, 0.99680256f, 0.99680256f};
    float out[3];
    TexelColour(texel, SurfaceMaterial{}, light, light::Grade{}, out);
    const float lin = light::SrgbToLinear(0.5f) * 0.99680256f;
    const float expectedIn[3] = {lin, lin, lin};
    float expected[3];
    light::GradePixel(expectedIn, light::Grade{}, expected);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(out[i], expected[i]);
}

TEST(SurfaceBake, MaterialColourIsLinearisedAndMultiplied) {
    const float texel[3] = {1.0f, 1.0f, 1.0f};
    const float light[3] = {0.3f, 0.3f, 0.3f};
    SurfaceMaterial m;
    m.albedo[0] = 0.5f;    // sRGB, as authored
    float out[3];
    TexelColour(texel, m, light, light::Grade{}, out);
    const float expectedIn[3] = {light::SrgbToLinear(0.5f) * 0.3f, 0.3f, 0.3f};
    float expected[3];
    light::GradePixel(expectedIn, light::Grade{}, expected);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(out[i], expected[i]);
}

TEST(SurfaceBake, FullyMetallicKeepsOnlyEmission) {
    const float texel[3] = {0.8f, 0.8f, 0.8f};
    const float light[3] = {5.0f, 5.0f, 5.0f};
    SurfaceMaterial m;
    m.metallic = 1.0f;
    m.emission[0] = 0.4f;   // linear, energy applied
    float out[3];
    TexelColour(texel, m, light, light::Grade{}, out);
    const float expectedIn[3] = {0.4f, 0.0f, 0.0f};
    float expected[3];
    light::GradePixel(expectedIn, light::Grade{}, expected);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(out[i], expected[i]);
}

TEST(SurfaceBake, UnshadedIgnoresTheLight) {
    const float texel[3] = {0.5f, 0.5f, 0.5f};
    const float dark[3] = {0.0f, 0.0f, 0.0f};
    SurfaceMaterial m;
    m.unshaded = true;
    float out[3];
    TexelColour(texel, m, dark, light::Grade{}, out);
    const float lin = light::SrgbToLinear(0.5f);
    const float expectedIn[3] = {lin, lin, lin};
    float expected[3];
    light::GradePixel(expectedIn, light::Grade{}, expected);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(out[i], expected[i]);
}

// ---- the page builder's headless pieces ----

TEST(SurfaceBake, SceneLinearIsTexelColourBeforeTheGrade) {
    const float texel[3] = {0.5f, 0.25f, 1.0f};
    const float light[3] = {0.3f, 0.6f, 0.9f};
    SurfaceMaterial m;
    m.emission[2] = 0.1f;
    float lin[3], graded[3], viaTexel[3];
    SceneLinear(texel, m, light, lin);
    light::GradePixel(lin, light::Grade{}, graded);
    TexelColour(texel, m, light, light::Grade{}, viaTexel);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(graded[i], viaTexel[i]);
    EXPECT_NEAR(lin[1], light::SrgbToLinear(0.25f) * 0.6f, 1e-7f);
}

TEST(SurfaceBake, TiersSplitAt25And40UnitsFromHome) {
    EXPECT_EQ(Tier(0.0f), 0);
    EXPECT_EQ(Tier(25.0f), 0);
    EXPECT_EQ(Tier(25.01f), 1);
    EXPECT_EQ(Tier(40.0f), 1);
    EXPECT_EQ(Tier(40.01f), 2);
}

TEST(SurfaceBake, DensityIsNativeClampedPerTier) {
    EXPECT_EQ(ChooseDensity(18.5f, 0), 18.5f);   // T0 keeps the texture's own detail
    EXPECT_EQ(ChooseDensity(105.0f, 0), 32.0f);  // ...up to its cap
    EXPECT_EQ(ChooseDensity(2.0f, 0), 8.0f);     // ...and never below the light floor
    EXPECT_EQ(ChooseDensity(30.0f, 1), 16.0f);
    EXPECT_EQ(ChooseDensity(30.0f, 2), 4.0f);
    EXPECT_EQ(ChooseDensity(0.0f, 2), 4.0f);     // untextured
}

TEST(SurfaceBake, RasteriseCoversEveryTexelInsideWithFourSubsamples) {
    // Right triangle (0,0) (4,0) (0,4) in texels: texels strictly below the
    // diagonal x + y < 4 at their subsample points.
    const float uv[3][2] = {{0, 0}, {4, 0}, {0, 4}};
    int calls = 0;
    std::vector<int> perTexel(16, 0);
    RasteriseTriangle(uv, 8, [&](int x, int y, const float bary[3]) {
        ++calls;
        ASSERT_LT(x, 4);
        ASSERT_LT(y, 4);
        EXPECT_NEAR(bary[0] + bary[1] + bary[2], 1.0f, 1e-5f);
        for (int i = 0; i < 3; ++i) EXPECT_GE(bary[i], -1e-5f);
        ++perTexel[y * 4 + x];
    });
    EXPECT_EQ(perTexel[0], 4);                 // (0,0): fully inside
    EXPECT_EQ(perTexel[3 * 4 + 3], 0);         // (3,3): beyond the diagonal
    EXPECT_EQ(perTexel[3 * 4 + 0], 3);         // (0,3): subsample (0.75,3.75) is outside
    // Subsamples at x+0.25 / x+0.75: 28 strictly inside x + y < 4, and 8 ON the
    // diagonal, which count (edges are inclusive; a texel averages whatever
    // lands in it, so a point shared by two triangles is harmless).
    EXPECT_EQ(calls, 36);
}

TEST(SurfaceBake, DilateFillsTwoTexelsAndNoMore) {
    const int n = 8;
    std::vector<uint8_t> rgb(n * n * 3, 0), mask(n * n, 0);
    mask[3 * n + 3] = 1;
    rgb[(3 * n + 3) * 3 + 0] = 200;
    Dilate(rgb, mask, n, 2);
    EXPECT_EQ(rgb[(3 * n + 5) * 3], 200);      // two to the right
    EXPECT_EQ(rgb[(1 * n + 1) * 3], 200);      // two diagonally
    EXPECT_EQ(rgb[(3 * n + 6) * 3], 0);        // three: untouched
    EXPECT_EQ(mask[3 * n + 6], 0);
}

TEST(SurfaceBake, ShelfPackPlacesRectsWithoutOverlapAndOpensPagesAsNeeded) {
    // Three 600x300 rects fill a 1024 page's first three shelves; a fourth
    // (and a 1024x1024) need new pages.
    const std::vector<PackRect> rects = {{600, 300}, {600, 300}, {600, 300}, {600, 300}, {1024, 1024},
                                         {100, 50}};
    int pages = 0;
    const std::vector<Placement> at = ShelfPack(rects, 1024, pages);
    ASSERT_EQ(at.size(), rects.size());
    EXPECT_EQ(pages, 3);
    for (std::size_t i = 0; i < at.size(); ++i) {
        EXPECT_GE(at[i].x, 0);
        EXPECT_GE(at[i].y, 0);
        EXPECT_LE(at[i].x + rects[i].w, 1024) << i;
        EXPECT_LE(at[i].y + rects[i].h, 1024) << i;
        EXPECT_EQ(at[i].x % 4, 0);   // DXT1 blocks never span two rects
        EXPECT_EQ(at[i].y % 4, 0);
        for (std::size_t j = 0; j < i; ++j) {
            if (at[i].page != at[j].page) continue;
            const bool apart = at[i].x + rects[i].w <= at[j].x || at[j].x + rects[j].w <= at[i].x ||
                               at[i].y + rects[i].h <= at[j].y || at[j].y + rects[j].h <= at[i].y;
            EXPECT_TRUE(apart) << i << " overlaps " << j;
        }
    }
}

// A rect larger than the page cannot be packed at all: ShelfPack says so
// (pages -1). Placed at (0, 0) of a fresh page instead, the one-page sets'
// shrink loop would halve a 1024 page to its 64 floor under a 415-texel chart,
// and the blood splatter plane's UVs would run to 6.5 (clamped edge stripes).
TEST(SurfaceBake, ShelfPackRefusesARectLargerThanThePage) {
    int pages = 0;
    ShelfPack({{415, 227}}, 256, pages);
    EXPECT_EQ(pages, -1);
    ShelfPack({{100, 50}, {80, 300}}, 256, pages);
    EXPECT_EQ(pages, -1) << "too tall";
    ShelfPack({{415, 227}}, 512, pages);
    EXPECT_EQ(pages, 1);
}

TEST(SurfaceBake, ALightSkipsSurfacesOutsideItsCullMask) {
    // scene_forward_clustered.glsl:1924: an omni light lights an instance only
    // when its cull mask shares a bit with the instance's layers.
    LightBvh empty;
    empty.Build({});
    BakedLight l = WhiteLightAbove(false);
    l.cullMask = 0x2u;   // layer 2 only
    float out[3];
    StaticLight({l}, empty, kPos, kUp, kEyeAbove, 1.0f, out, /*layers=*/0x1u);
    for (float v : out) EXPECT_EQ(v, 0.0f);
    StaticLight({l}, empty, kPos, kUp, kEyeAbove, 1.0f, out, /*layers=*/0x3u);
    for (float v : out) EXPECT_NEAR(v, 2.0f * 0.49840128f, 1e-5f);
}

// A screen's icons read like text (the health counter, the round indicator):
// those props keep their texture's own detail, far above the T0 cap, up to the
// detail cap.
TEST(SurfaceBake, ADetailSurfaceKeepsItsTexturesDetail) {
    EXPECT_EQ(ChooseDetailDensity(200.0f), 200.0f);
    EXPECT_EQ(ChooseDetailDensity(5000.0f), kDetailDensityCap);
    EXPECT_EQ(ChooseDetailDensity(2.0f), 8.0f);   // the light floor still holds
    EXPECT_GT(kDetailDensityCap, 32.0f);
}

// The yes/no keys drew "YES"/"NO" (the model's own texture) where Godot draws a
// check and a cross: main.tscn gives the surface misc/yes_no_stereo_lingual.png.
// A texture the SCENE authored (not one of the model's) is what the bake samples.
TEST(SurfaceBake, ASceneAuthoredTextureIsTheOneDrawn) {
    EXPECT_TRUE(IsSceneAuthoredTexture("inline_textures/misc_yes_no_stereo_lingual.png"));
    EXPECT_FALSE(IsSceneAuthoredTexture(
        "inline_textures/models_double_nothing_machine_double_nothing_machine1_metal_sheet_double_nothing.png"));
    EXPECT_FALSE(IsSceneAuthoredTexture(""));
}
