// Godot 4.1.4's light and output maths, pinned to hand-computed numbers.
// Every expected value below is worked from the cited source line, not from
// the implementation, so a transcription error fails here.
#include "assets/light_model.h"

#include <cmath>

#include <gtest/gtest.h>

using namespace assets::light;

TEST(LightModel, SrgbToLinearMatchesGodot) {
    EXPECT_NEAR(SrgbToLinear(0.86f), 0.71057f, 1e-4f);   // light_tabletop interior red
    EXPECT_NEAR(SrgbToLinear(0.12f), 0.013412f, 1e-5f);  // ...its blue: the amber
    EXPECT_NEAR(SrgbToLinear(0.03f), 0.03f / 12.92f, 1e-7f);
}

TEST(LightModel, LinearToSrgbIsTheClampedCurve) {
    // tonemap.glsl:247: below 0.0031308 the linear segment.
    EXPECT_NEAR(LinearToSrgb(0.0015654f), 12.92f * 0.0015654f, 1e-7f);
    EXPECT_NEAR(LinearToSrgb(0.5f), 0.735357f, 1e-5f);
    EXPECT_NEAR(LinearToSrgb(2.0f), 1.0f, 1e-6f);   // 1.055 - 0.055 in float
    EXPECT_EQ(LinearToSrgb(-1.0f), 0.0f);
}

TEST(LightModel, OmniAttenuationIsGodots) {
    // (1-(2/10)^4)^2 * 2^-1
    EXPECT_NEAR(OmniAttenuation(2.0f, 10.0f, 1.0f), 0.49840128f, 1e-6f);
    EXPECT_EQ(OmniAttenuation(10.0f, 10.0f, 1.0f), 0.0f);
}

TEST(LightModel, BurleyFacingTheLampIsOne) {
    const float n[3] = {0, 1, 0};
    EXPECT_NEAR(BurleyNL(n, n, n, 1.0f), 1.0f, 1e-6f);   // Schlick(1)=0 -> FdV=FdL=1
}

TEST(LightModel, BurleyIsBlackAtGrazing) {
    const float n[3] = {0, 1, 0}, l[3] = {1, 0, 0};
    EXPECT_EQ(BurleyNL(n, l, n, 1.0f), 0.0f);
}

TEST(LightModel, BurleySixtyDegreesRoughnessHalf) {
    // N.L = 0.5, V = N, L.H = cos 30deg; FD90-1 = 2*0.75*0.5-0.5 = 0.25;
    // FdV = 1, FdL = 1 + 0.25*0.5^5 = 1.0078125; result = 1.0078125 * 0.5
    const float n[3] = {0, 1, 0}, l[3] = {0.8660254f, 0.5f, 0};
    EXPECT_NEAR(BurleyNL(n, l, n, 0.5f), 0.50390625f, 1e-5f);
}

TEST(LightModel, AcesWhiteMapsToOneAndHalfToGodotsValue) {
    float c[3] = {1, 1, 1}, o[3];
    Aces(c, 1.0f, o);
    EXPECT_NEAR(o[0], 1.0f, 1e-4f);
    // Both matrices' rows sum to 1, so grey stays grey: f(0.9) / f(1.8).
    const float h[3] = {0.5f, 0.5f, 0.5f};
    Aces(h, 1.0f, o);
    EXPECT_NEAR(o[1], 0.747779f, 1e-4f);
}

TEST(LightModel, BcsIsTheEnvironmentsGrade) {
    float c[3] = {0.4f, 0.2f, 0.1f};
    Bcs(c, 1.49f, 0.8f, 0.26f);
    EXPECT_NEAR(c[0], 0.429787f, 1e-5f);
    EXPECT_NEAR(c[1], 0.367803f, 1e-5f);
    EXPECT_NEAR(c[2], 0.336811f, 1e-5f);
}

TEST(LightModel, PosterizeSevenLevels) {
    EXPECT_NEAR(Posterize(0.5f, 7.0f), 3.0f / 7.0f, 1e-6f);
    EXPECT_EQ(Posterize(1.0f, 7.0f), 1.0f);
}

TEST(LightModel, StoreUnorm8IsTheRgba8Target) {
    // The BCS result is written to an RGBA8 target and posterize reads it back
    // as byte / 255, so a float a hair under 1 is 1 again, and posterizes to
    // 1, not to 6/7.
    EXPECT_EQ(StoreUnorm8(0.99999994f), 1.0f);
    EXPECT_EQ(StoreUnorm8(1.5f), 1.0f);
    EXPECT_EQ(StoreUnorm8(-0.2f), 0.0f);
    EXPECT_EQ(StoreUnorm8(0.5f), 128.0f / 255.0f);
}

TEST(LightModel, GradePixelBlackIsBlack) {
    // bcs(0) = 0.5 - 0.8*0.5 = 0.1 grey; floor(0.7)/7 = 0.
    const float in[3] = {0, 0, 0};
    float out[3];
    GradePixel(in, Grade{}, out);
    for (float v : out) EXPECT_EQ(v, 0.0f);
}

TEST(LightModel, GradePixelClampsBeforePosterize) {
    // bcs(1) = 0.5 + 0.8*(1.49-0.5) = 1.292; the RGBA8 target clamps it to 1.
    const float in[3] = {1, 1, 1};
    float out[3];
    GradePixel(in, Grade{}, out);
    for (float v : out) EXPECT_EQ(v, 1.0f);
}

TEST(LightModel, GradePixelIsTheChainInGodotsOrder) {
    // tonemap.glsl:455-490: ACES -> linear_to_srgb -> BCS -> (RGBA8 clamp) ->
    // posterize. A dark value, so no stage saturates and the order shows.
    const float in[3] = {0.05f, 0.05f, 0.05f};
    float out[3];
    GradePixel(in, Grade{}, out);
    float a[3];
    Aces(in, 1.0f, a);
    float s[3] = {LinearToSrgb(a[0]), LinearToSrgb(a[1]), LinearToSrgb(a[2])};
    Bcs(s, 1.49f, 0.8f, 0.26f);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(out[i], Posterize(StoreUnorm8(s[i]), 7.0f));
    }
}

TEST(LightModel, SizeAIsGodotsSoftLightTerm) {
    // scene_forward_lights_inc.glsl:577-578: t = size / d; 1 - 1/sqrt(1 + t^2).
    EXPECT_NEAR(SizeA(3.26f, 3.26f), 1.0f - 1.0f / std::sqrt(2.0f), 1e-6f);
    EXPECT_EQ(SizeA(0.0f, 2.0f), 0.0f);
}

TEST(LightModel, BurleySizeALiftsTheTerminator) {
    // A adds to N.L and L.H (:95, :110): at grazing a sized light still lights.
    const float n[3] = {0, 1, 0}, l[3] = {1, 0, 0};
    // NdotL = min(0.25 + 0, 1) = 0.25; V = N so FdV = 1; H = normalize(0.5,0.5,0)
    // L.H = 0.70711, cLdotH = 0.95711; FD90-1 = 2*0.91606*1 - 0.5 = 1.33212;
    // FdL = 1 + 1.33212*(0.75)^5 = 1.31612; result = 1.31612 * 0.25
    EXPECT_NEAR(BurleyNL(n, l, n, 1.0f, 0.25f), 0.32903f, 1e-4f);
}

// scene_forward_clustered.glsl:888-891, binormal from :300-301.
TEST(LightModel, PerturbNormalFlatMapKeepsTheNormal) {
    const float n[3] = {0, 1, 0}, t[4] = {1, 0, 0, 1};
    const float map[2] = {0.5f, 0.5f};   // (0,0) once unpacked: z = 1
    float out[3];
    PerturbNormal(n, t, map, 1.0f, out);
    EXPECT_NEAR(out[0], 0.0f, 1e-6f);
    EXPECT_NEAR(out[1], 1.0f, 1e-6f);
    EXPECT_NEAR(out[2], 0.0f, 1e-6f);
}

TEST(LightModel, PerturbNormalYZeroTurnsItToMinusBinormal) {
    // The shipped DXT5_RA_AS_RG normal maps have Y (alpha) = 0: y = -1, z = 0,
    // so the normal becomes -binormal. binormal = cross(N, T) * sign(w).
    const float n[3] = {0, 1, 0}, t[4] = {1, 0, 0, 1};
    const float map[2] = {0.5f, 0.0f};
    float out[3];
    PerturbNormal(n, t, map, 1.0f, out);
    // cross((0,1,0),(1,0,0)) = (0,0,-1); -binormal = (0,0,1)
    EXPECT_NEAR(out[0], 0.0f, 1e-6f);
    EXPECT_NEAR(out[1], 0.0f, 1e-6f);
    EXPECT_NEAR(out[2], 1.0f, 1e-6f);
    const float tNeg[4] = {1, 0, 0, -1};
    PerturbNormal(n, tNeg, map, 1.0f, out);
    EXPECT_NEAR(out[2], -1.0f, 1e-6f);
}

TEST(LightModel, PerturbNormalDepthMixesTowardTheMap) {
    // normalize(mix(N, map, 0.5)): N=(0,1,0), map=(0,0,1) -> (0,.7071,.7071)
    const float n[3] = {0, 1, 0}, t[4] = {1, 0, 0, 1};
    const float map[2] = {0.5f, 0.0f};
    float out[3];
    PerturbNormal(n, t, map, 0.5f, out);
    EXPECT_NEAR(out[1], 0.70710677f, 1e-6f);
    EXPECT_NEAR(out[2], 0.70710677f, 1e-6f);
}

// An animated light is baked per vertex at energy 1 as a scalar and stored in
// one RGBA8 channel as 255 * sqrt(clamp(v / 4, 0, 1)); the vertex stage
// decodes it as col * col * 4.
TEST(LightModel, DynLayerEncodingRoundTrips) {
    EXPECT_EQ(EncodeDynLayer(0.0f), 0);
    EXPECT_EQ(EncodeDynLayer(4.0f), 255);
    EXPECT_EQ(EncodeDynLayer(100.0f), 255);
    EXPECT_EQ(EncodeDynLayer(-1.0f), 0);
    EXPECT_EQ(EncodeDynLayer(1.0f), 128);   // sqrt(0.25) * 255 = 127.5
    const float back = DecodeDynLayer(EncodeDynLayer(1.0f));
    EXPECT_NEAR(back, 1.0f, 0.02f);
}

// u_dynN = mix(grey, srgb_to_linear(colour), saturation) * energy * k. The
// flash is added before Godot's grade, whose saturation (0.26) greys it: on
// the felt Godot's muzzle flash adds (+0.32, +0.32, +0.25), not orange.
TEST(LightModel, DynUniformIsTheGradesDesaturatedColourTimesEnergyTimesGain) {
    const float colour[3] = {1.0f, 0.5f, 0.0f};
    float out[3];
    DynUniform(colour, 3.0f, 0.25f, out);
    const float lin[3] = {1.0f, SrgbToLinear(0.5f), 0.0f};
    const float luma = (lin[0] + lin[1] + lin[2]) * 0.33333f;   // Bcs's grey (tonemap.glsl:339)
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(out[i], (luma + 0.26f * (lin[i] - luma)) * 0.75f, 1e-6f) << i;
}

// BCS is affine in the pre-BCS colour for any brightness b and contrast k, so
// the baked out0 (at b0, k0) maps exactly:
//   out = s * out0 + o,  s = b k / (b0 k0),  o = 0.5 (1 - k) - s * 0.5 (1 - k0)
TEST(LightModel, EnvironmentUniformsAreTheExactAffineBcs) {
    float s = 0.0f, o = 0.0f;
    EnvironmentUniforms(1.49f, 0.8f, &s, &o);
    EXPECT_FLOAT_EQ(s, 1.0f);
    EXPECT_NEAR(o, 0.0f, 1e-7f);
    EnvironmentUniforms(3.49f, 0.8f, &s, &o);   // brightness changed, contrast at rest
    EXPECT_NEAR(s, 3.49f / 1.49f, 1e-6f);
    EXPECT_NEAR(o, 0.1f * (1.0f - 3.49f / 1.49f), 1e-6f);
    // Against Bcs itself: grade a colour at (b, k) and at rest, and map.
    const float c[3] = {0.3f, 0.5f, 0.7f};
    float rest[3] = {c[0], c[1], c[2]}, moved[3] = {c[0], c[1], c[2]};
    Bcs(rest, 1.49f, 0.8f, 0.26f);
    Bcs(moved, 1.49f, 2.65f, 0.26f);
    EnvironmentUniforms(1.49f, 2.65f, &s, &o);
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(rest[i] * s + o, moved[i], 1e-5f) << i;
}

// The mip the GPU samples a normal map at from the `home` camera (the shipped
// DXT5_RA_AS_RG maps have Y = 0 in mip 0 only, so which mip is sampled decides
// the look). One screen pixel at distance d covers 2 d tan(fov / 2) / height
// world units, stretched by 1 / cos(theta) on a slanted surface;
// lod = log2(texels per unit * that).
TEST(LightModel, NormalMapLodIsLog2OfTexelsPerScreenPixel) {
    const float px = 2.0f * 10.0f * std::tan(65.5f * 3.14159265f / 360.0f) / 540.0f;
    EXPECT_NEAR(NormalMapLod(1.0f / px, 10.0f, 1.0f), 0.0f, 1e-4f);         // one texel a pixel
    EXPECT_NEAR(NormalMapLod(4.0f / px, 10.0f, 1.0f), 2.0f, 1e-4f);         // four: two levels down
    EXPECT_NEAR(NormalMapLod(1.0f / px, 10.0f, 0.5f), 1.0f, 1e-4f);         // slanted 60 degrees
    EXPECT_NEAR(NormalMapLod(0.25f / px, 10.0f, 1.0f), -2.0f, 1e-4f);       // magnified
}

// The fades take brightness to 0.01, where the affine form leaves every texel
// at a flat grey 0.1, but Godot's posterize (floor(c * 7) / 7) floors that to
// black. The baked texels sit on the 7 levels, so the uniforms are fitted to
// what posterize does to those levels: black at the fade's end, identity at
// rest.
TEST(LightModel, PosterizedEnvironmentUniformsFadeToBlackAndRestAtIdentity) {
    float s = 0.0f, o = 0.0f;
    PosterizedEnvironmentUniforms(1.49f, 0.8f, &s, &o);
    EXPECT_NEAR(s, 1.0f, 1e-5f);
    EXPECT_NEAR(o, 0.0f, 1e-5f);
    PosterizedEnvironmentUniforms(0.01f, 0.8f, &s, &o);
    EXPECT_NEAR(s, 0.0f, 1e-5f);
    EXPECT_NEAR(o, 0.0f, 1e-5f);
    // Brightening (adrenaline, revival) keeps the exact affine form: a fit over
    // the levels flattens it, since the bright ones clamp at 1.
    PosterizedEnvironmentUniforms(3.49f, 0.8f, &s, &o);
    float es = 0.0f, eo = 0.0f;
    EnvironmentUniforms(3.49f, 0.8f, &es, &eo);
    EXPECT_FLOAT_EQ(s, es);
    EXPECT_FLOAT_EQ(o, eo);
}

// A point light on a billboard: the zippo's flame and the cigarette's smoke
// are particles Godot lights with the zippo's animated light. The normal of a
// camera-facing quad is unknown, so the Lambert-wrap term is taken at
// N.L = 0, a quarter.
TEST(LightModel, APointLightAddsGodotsAttenuationAtAQuarter) {
    assets::light::PointLight l;
    l.pos[0] = 1.0f;
    l.linear[0] = 2.0f;
    l.linear[1] = 1.0f;
    l.range = 4.0f;
    l.decay = 1.0f;
    const float at[3] = {0.0f, 0.0f, 0.0f};
    float c[3] = {0.1f, 0.1f, 0.1f};
    assets::light::AddPointLight(l, at, c);
    const float a = assets::light::OmniAttenuation(1.0f, 4.0f, 1.0f) * 0.25f;
    EXPECT_NEAR(c[0], 0.1f + 2.0f * a, 1e-6f);
    EXPECT_NEAR(c[1], 0.1f + 1.0f * a, 1e-6f);
    EXPECT_NEAR(c[2], 0.1f, 1e-6f);
}

TEST(LightModel, APointLightBeyondItsRangeAddsNothing) {
    assets::light::PointLight l;
    l.pos[0] = 10.0f;
    l.linear[0] = l.linear[1] = l.linear[2] = 5.0f;
    l.range = 4.0f;
    const float at[3] = {0.0f, 0.0f, 0.0f};
    float c[3] = {0.0f, 0.0f, 0.0f};
    assets::light::AddPointLight(l, at, c);
    EXPECT_EQ(c[0], 0.0f);
}

// Godot blends a transparent quad in linear HDR, before the tonemap: lit far
// above 1, a quad at alpha 0.055 still saturates (the zippo's flame). In an
// 8-bit target the same is its alpha raised by how far over 1 it is lit.
TEST(LightModel, AQuadLitAboveOneBlendsAsIfInHdr) {
    const float dim[3] = {0.4f, 0.3f, 0.2f};
    EXPECT_FLOAT_EQ(assets::light::HdrBlendAlpha(dim, 0.055f), 0.055f) << "smoke in room light is unchanged";
    const float hot[3] = {10.0f, 5.0f, 1.0f};
    EXPECT_NEAR(assets::light::HdrBlendAlpha(hot, 0.055f), 0.55f, 1e-5f);
    const float flame[3] = {50.0f, 26.0f, 0.0f};
    EXPECT_FLOAT_EQ(assets::light::HdrBlendAlpha(flame, 0.055f), 1.0f);
}
