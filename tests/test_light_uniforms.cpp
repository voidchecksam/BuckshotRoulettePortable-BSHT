// The animated lights' uniforms, read from the live tree.
#include "app/light_uniforms.h"

#include "assets/light_model.h"
#include "assets/moving_lights.h"
#include "assets/scene_fog.h"
#include "godot/runtime_scene_loader.h"
#include "render/shaders/surface.h"
#include "render/shaders/surface_batched.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cctype>

#include <string>

namespace {

std::string ExportPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_main.json";
}

}  // namespace

TEST(LightUniforms, NothingBurnsAtRest) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    app::DynLights lights;
    lights.Bind(t);
    EXPECT_EQ(lights.Bound(), 4);
    float u[4][3];
    EXPECT_FALSE(lights.Uniforms(1.0f, u));
    for (const auto& c : u)
        for (float v : c) EXPECT_FLOAT_EQ(v, 0.0f);
}

TEST(LightUniforms, TheMuzzleFlashAtThreeIsItsDynUniformAtThree) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    auto* flash = t.get_node_as<godot::OmniLight3D>("tabletop parent/shotgun main parent/light_muzzle flash LS");
    ASSERT_NE(flash, nullptr);
    flash->set_light_energy(3.0f);
    app::DynLights lights;
    lights.Bind(t);
    float u[4][3];
    ASSERT_TRUE(lights.Uniforms(0.5f, u));
    const godot::Color c = flash->get_light_color();
    const float srgb[3] = {c.r, c.g, c.b};
    float expected[3];
    assets::light::DynUniform(srgb, 3.0f, 0.5f, expected);
    for (int i = 0; i < 3; ++i) EXPECT_FLOAT_EQ(u[0][i], expected[i]) << i;
    EXPECT_FLOAT_EQ(u[1][0], 0.0f);   // the barrel mask is still dark
}

TEST(LightUniforms, AHiddenLightDoesNotBurn) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    // The zippo's parent is hidden until "player use cigarettes" shows it.
    auto* zippo = t.get_node_as<godot::OmniLight3D>(
        "player item interaction parent/zippo lighter parent2/light_zippo flame");
    ASSERT_NE(zippo, nullptr);
    zippo->set_light_energy(18.0f);
    app::DynLights lights;
    lights.Bind(t);
    float u[4][3];
    EXPECT_EQ(lights.Uniforms(1.0f, u), zippo->is_visible_in_tree());
}

TEST(LightUniforms, TheEnvironmentGradeIsOneAndZeroAtRestAndFollowsTheBrightness) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    app::EnvironmentGrade grade;
    grade.Bind(t);
    float s = 0.0f, o = 0.0f;
    ASSERT_TRUE(grade.Uniforms(&s, &o));
    EXPECT_NEAR(s, 1.0f, 1e-6f);
    EXPECT_NEAR(o, 0.0f, 1e-6f);
    t.get_node_as<godot::WorldEnvironment>("WorldEnvironment")->set_adjustment_brightness(0.01f);
    ASSERT_TRUE(grade.Uniforms(&s, &o));
    float es = 0.0f, eo = 0.0f;
    assets::light::PosterizedEnvironmentUniforms(0.01f, 0.8f, &es, &eo);
    EXPECT_FLOAT_EQ(s, es);
    EXPECT_FLOAT_EQ(o, eo);
}

// A freed light (a subtree queue_free'd by a script) is dropped, not read
// through: the bindings re-resolve by path once the tree has freed nodes.
TEST(LightUniforms, AFreedLightIsDroppedNotReadThrough) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    app::DynLights lights;
    lights.Bind(t);
    ASSERT_EQ(lights.Bound(), 4);
    t.get_node("player item interaction parent/zippo lighter parent2")->queue_free();
    t.process_frame(1.0 / 60.0);
    EXPECT_EQ(lights.Bound(), 3);
    float u[4][3];
    EXPECT_FALSE(lights.Uniforms(1.0f, u));
}

// Heaven rests at brightness 1.26, contrast 1.46, and its pages were baked
// with that grade, so at rest the runtime grade must be (1, 0) there too.
// Measured against main's rest (1.49, 0.8), heaven would draw at scale 1.54,
// offset -0.38: a second grade on top of the baked one.
TEST(LightUniforms, TheEnvironmentGradeIsRelativeToTheScenesOwnRest) {
    const std::string heaven = std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_heaven.json";
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(heaven.c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "runtime_heaven.json not exported";
    }
    auto* env = t.get_node_as<godot::WorldEnvironment>("WorldEnvironment");
    ASSERT_NE(env, nullptr);
    ASSERT_GT(std::fabs(env->get_adjustment_brightness() - 1.49f), 0.1f) << "heaven's rest differs from main's";
    app::EnvironmentGrade grade;
    grade.Bind(t);
    float s = 0.0f, o = 0.0f;
    ASSERT_TRUE(grade.Uniforms(&s, &o));
    EXPECT_NEAR(s, 1.0f, 1e-5f);
    EXPECT_NEAR(o, 0.0f, 1e-5f);
}

// Heaven is mostly fog (exponential 0.0035, light colour x1.48, sky affected)
// and the background is the fog. The runtime grades the fog colour exactly as
// the bake graded the pages, and clears to it.
TEST(LightUniforms, HeavensFogIsItsColourGradedLikeItsPages) {
    const std::string heaven = std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_heaven.json";
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(heaven.c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "runtime_heaven.json not exported";
    }
    auto* env = t.get_node_as<godot::WorldEnvironment>("WorldEnvironment");
    ASSERT_NE(env, nullptr);
    if (!env->get_fog_enabled()) GTEST_SKIP() << "runtime_heaven.json predates the fog keys";
    EXPECT_NEAR(env->get_fog_density(), 0.0035f, 1e-6f);
    EXPECT_NEAR(env->get_fog_light_energy(), 1.48f, 1e-5f);
    EXPECT_NEAR(env->get_fog_sky_affect(), 1.0f, 1e-6f);

    app::EnvironmentGrade grade;
    grade.Bind(t);
    app::EnvironmentGrade::Fog fog;
    ASSERT_TRUE(grade.FogUniforms(&fog));
    EXPECT_NEAR(fog.density, 0.0035f, 1e-6f);

    const godot::Color c = env->get_fog_light_color();
    const float lin[3] = {assets::light::SrgbToLinear(c.r) * 1.48f, assets::light::SrgbToLinear(c.g) * 1.48f,
                          assets::light::SrgbToLinear(c.b) * 1.48f};
    assets::light::Grade g;
    g.brightness = env->get_adjustment_brightness();
    g.contrast = env->get_adjustment_contrast();
    g.saturation = env->get_adjustment_saturation();
    float want[3];
    assets::light::GradePixel(lin, g, want);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(fog.colour[i], want[i], 1e-5f) << i;
        EXPECT_NEAR(fog.clear[i], want[i], 1e-5f) << i;   // sky_affect 1: the background is the fog
        EXPECT_GT(fog.colour[i], 0.6f) << "heaven reads near-white in Godot";
    }
    // The FOG program's mix (assets::FogWeight): the grade's grey curve, the
    // fog's linear luminance, and the grey of the colour the program mixes
    // toward, the graded, posterized fog (the menu's reads 0 where its curve
    // gives 0.10; weighing against 0.10 pushes its dark shells fully into fog).
    const float lum = 0.2126f * lin[0] + 0.7152f * lin[1] + 0.0722f * lin[2];
    const assets::FogCurve curve = assets::FogCurveOf(g);
    EXPECT_NEAR(fog.mix.luminance, lum, 1e-5f);
    EXPECT_NEAR(fog.mix.grey, 0.2126f * want[0] + 0.7152f * want[1] + 0.0722f * want[2], 1e-5f);
    EXPECT_NEAR(fog.mix.curve.whiteInv, curve.whiteInv, 1e-6f);
    EXPECT_NEAR(fog.mix.curve.brightness, g.brightness, 1e-6f);
    EXPECT_NEAR(fog.mix.curve.contrast, g.contrast, 1e-6f);
}

// The menu's fog grades to black (posterized) where the grade's curve gives
// it 0.10: the FOG program mixes toward black, so it must weigh against
// black, or its dark shells drown (frame 700 read 3.2 where Godot reads 5.7).
TEST(LightUniforms, TheMenusFogMixesTowardTheBlackItShows) {
    const std::string menu = std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_menu.json";
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(menu.c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "runtime_menu.json not exported";
    }
    app::EnvironmentGrade grade;
    grade.Bind(t);
    app::EnvironmentGrade::Fog fog;
    ASSERT_TRUE(grade.FogUniforms(&fog));
    for (float c : fog.colour) EXPECT_EQ(c, 0.0f);
    EXPECT_GT(assets::FogGrey(fog.mix.luminance, fog.mix.curve), 0.05f) << "the curve alone is not black";
    EXPECT_EQ(fog.mix.grey, 0.0f);
}

TEST(LightUniforms, TheTableHasNoFog) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    app::EnvironmentGrade grade;
    grade.Bind(t);
    app::EnvironmentGrade::Fog fog;
    EXPECT_FALSE(grade.FogUniforms(&fog));
}

// Godot mixes fog into the linear colour before the grade; the port mixes the
// graded texel. A vertex carries its surface's linear luminance in a_col.a
// (log2 over 20 octaves from 2^-10) so the FOG program can find the weight
// that gives the grade of Godot's mix.
TEST(SceneFog, TheSurfaceLuminanceByteRoundTripsAcrossItsRange) {
    for (float x : {0.002f, 0.01f, 0.1f, 1.0f, 10.0f, 500.0f}) {
        const float back = assets::DecodeFogLuminance(assets::EncodeFogLuminance(x));
        EXPECT_NEAR(back, x, 0.06f * x) << x;
    }
    EXPECT_EQ(assets::EncodeFogLuminance(1e9f), 255);
    EXPECT_EQ(assets::EncodeFogLuminance(0.0f), 0);
    EXPECT_EQ(assets::EncodeFogLuminance(-1.0f), 0);
    EXPECT_LE(assets::DecodeFogLuminance(0), 0.001f);
}

// The FOG program's grey curve (transcribed from assets::FogGrey) is the
// scene's grade of a grey: ACES keeps a grey grey (its matrices' rows sum to
// 1), saturation leaves it alone, the target clamps it.
TEST(SceneFog, TheGreyCurveIsTheGradeOfAGrey) {
    assets::light::Grade g;
    g.brightness = 1.49f;   // the menu's
    g.contrast = 0.95f;
    g.saturation = 0.36f;
    const assets::FogCurve c = assets::FogCurveOf(g);
    for (float y : {0.0f, 0.002f, 0.03f, 0.2f, 1.0f, 4.0f, 50.0f}) {
        const float in[3] = {y, y, y};
        float v[3];
        assets::light::Aces(in, g.white, v);
        for (float& s : v) s = assets::light::LinearToSrgb(s);
        assets::light::Bcs(v, g.brightness, g.contrast, g.saturation);
        EXPECT_NEAR(assets::FogGrey(y, c), std::clamp(v[1], 0.0f, 1.0f), 2e-4f) << y;
    }
}

// The weight mixes the graded surface toward the graded fog so the result is
// the grade of Godot's linear mix t x + (1 - t) f, in black fog (the menu) and
// bright fog (heaven) alike.
TEST(SceneFog, TheFogWeightGivesTheGradeOfGodotsLinearMix) {
    const assets::FogCurve c = assets::FogCurveOf(assets::light::Grade{});
    for (float fog : {0.0f, 1.5f}) {
        const float fogGrey = assets::FogGrey(fog, c);
        for (float x : {0.05f, 0.4f, 3.0f}) {
            const float surface = assets::FogGrey(x, c);
            if (std::fabs(surface - fogGrey) < 0.01f) continue;   // both read the same: any weight
            for (float t : {0.02f, 0.3f, 0.9f}) {
                const float w = assets::FogWeight(t, x, fog, fogGrey, c);
                const float shown = surface + (fogGrey - surface) * w;
                EXPECT_NEAR(shown, assets::FogGrey(t * x + (1.0f - t) * fog, c), 1e-4f)
                    << "fog " << fog << " x " << x << " t " << t;
            }
        }
        EXPECT_NEAR(assets::FogWeight(1.0f, 0.4f, fog, fogGrey, c), 0.0f, 1e-5f) << "no fog at the eye";
        EXPECT_NEAR(assets::FogWeight(0.0f, 0.4f, fog, fogGrey, c), 1.0f, 1e-5f) << "all fog at infinity";
    }
}

// How long a triangle edge may run in a runtime-fogged scene: as far as the
// transmission changes 5% (heaven, 0.0035: 14.3 units), never under 2 units
// (the menu's dense fog would tessellate its shells) or over 16.
TEST(SceneFog, AFogEdgeSpansAtMostAFewPercentOfTheFog) {
    EXPECT_NEAR(assets::FogMaxEdge(0.0035f), 0.05f / 0.0035f, 1e-3f);
    EXPECT_FLOAT_EQ(assets::FogMaxEdge(0.6255f), 2.0f);
    EXPECT_FLOAT_EQ(assets::FogMaxEdge(0.0001f), 16.0f);
    EXPECT_FLOAT_EQ(assets::FogMaxEdge(0.0f), 0.0f) << "no fog: nothing to tessellate";
}

namespace {

// The FOG programs' fog block: from its comment to the v_fog line.
std::string FogBlock(const std::string& src) {
    const std::size_t from = src.find("// Godot's fog, mixed in LINEAR light");
    const std::size_t vfog = src.find("v_fog = abs(span)", from == std::string::npos ? 0 : from);
    if (from == std::string::npos || vfog == std::string::npos) return {};
    return src.substr(from, src.find('\n', vfog) - from);
}

// Every float literal in a GLSL fragment.
std::vector<float> FloatLiterals(const std::string& glsl) {
    std::vector<float> out;
    for (std::size_t i = 0; i < glsl.size();) {
        const bool starts = std::isdigit(static_cast<unsigned char>(glsl[i])) &&
                            (i == 0 || !(std::isalnum(static_cast<unsigned char>(glsl[i - 1])) || glsl[i - 1] == '_'));
        if (!starts) {
            ++i;
            continue;
        }
        std::size_t used = 0;
        out.push_back(std::stof(glsl.substr(i), &used));
        i += used;
    }
    return out;
}

bool Has(const std::vector<float>& literals, float v) {
    for (float l : literals) {
        if (std::fabs(l - v) <= 1e-6f * std::max(1.0f, std::fabs(v))) return true;
    }
    return false;
}

}  // namespace

// The shaders repeat FogGrey / FogWeight's constants by hand. Both FOG
// programs carry the same block, its decode is the C++ encoding's, and every
// constant the C++ uses appears in it.
TEST(SceneFog, TheFogProgramsTranscribeTheCpp) {
    const std::string unbatched = FogBlock(render::kSurfaceFogVertexSrc);
    const std::string batched = FogBlock(render::kBatchedFogVertexSrc);
    ASSERT_FALSE(unbatched.empty());
    EXPECT_EQ(unbatched, batched) << "the two FOG programs drifted apart";
    char decode[96];
    std::snprintf(decode, sizeof decode, "exp2(a_col.a * %.1f - %.1f)", static_cast<double>(assets::kFogLuminanceOctaves),
                  static_cast<double>(-assets::kFogLuminanceFloor));
    EXPECT_NE(unbatched.find(decode), std::string::npos) << decode;
    const std::vector<float> literals = FloatLiterals(unbatched);
    for (float c : {assets::light::kAcesA, assets::light::kAcesB, assets::light::kAcesC, assets::light::kAcesD,
                    assets::light::kAcesE, assets::light::kAcesBias, assets::kFogSpanFloor}) {
        EXPECT_TRUE(Has(literals, c)) << c;
    }
}

// A display-space mix keeps t of a surface. In the menu's black fog Godot keeps
// G(t x) / G(x) of it, about twice that for a dark one (the shells read 3.1
// where Godot's read 5.7, frame 700).
TEST(SceneFog, ADarkSurfaceInBlackFogKeepsMoreThanItsTransmission) {
    const assets::FogCurve c = assets::FogCurveOf(assets::light::Grade{});
    const float t = 0.1f;
    const float w = assets::FogWeight(t, 0.3f, 0.0f, assets::FogGrey(0.0f, c), c);
    EXPECT_GT(1.0f - w, 1.8f * t);
}

// The street light casts no runtime shadow, so only the car is in its scope;
// otherwise it lights the restroom through its walls.
TEST(MovingLight, OnlyTheCarIsInItsScope) {
    EXPECT_TRUE(assets::InMovingLightScope("player vehicle parent/vehicle/seat"));
    EXPECT_FALSE(assets::InMovingLightScope("restroom_CLUB/bathroom wall main"));
    EXPECT_FALSE(assets::InMovingLightScope("player vehicle parent"));
}

// A prop baked at its own grade (the car, at the ending's) is graded against
// that grade, so the renderer needs the live brightness and contrast as well as
// the scale and offset relative to the scene's rest.
TEST(LightUniforms, TheEnvironmentGradeReportsTheLiveGrade) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    app::EnvironmentGrade grade;
    grade.Bind(t);
    auto* env = t.get_node_as<godot::WorldEnvironment>("WorldEnvironment");
    ASSERT_NE(env, nullptr);
    env->set_adjustment_brightness(5.68f);   // EndingManager.gd:120-121
    env->set_adjustment_contrast(1.3f);
    float b = 0.0f, k = 0.0f;
    ASSERT_TRUE(grade.Live(&b, &k));
    EXPECT_FLOAT_EQ(b, 5.68f);
    EXPECT_FLOAT_EQ(k, 1.3f);
    float s = 0.0f, o = 0.0f;
    assets::light::PosterizedEnvironmentUniforms(b, k, &s, &o, 5.68f, 1.3f);
    EXPECT_NEAR(s, 1.0f, 1e-6f) << "a prop baked at the ending's grade draws its texels as baked";
    EXPECT_NEAR(o, 0.0f, 1e-6f);
}
