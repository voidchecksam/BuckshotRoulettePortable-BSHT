// Godot 4.1.4's light model and output chain, transcribed. The one home of
// these formulas: the offline surface bake grades every texel with them, and
// nothing on the device evaluates them.
//
// Each function cites the Godot line it transcribes, at the 4.1.4-stable tag.
//
// Headless and allocation-free, so it is unit-tested on its own.
#pragma once

namespace assets::light {

// Color::srgb_to_linear, core/math/color.h (the exact piecewise curve).
// Clamped to [0, 1], which is all an authored colour can be.
float SrgbToLinear(float c);

// tonemap.glsl:247 linear_to_srgb, which clamps to [0, 1] first.
float LinearToSrgb(float c);

// scene_forward_lights_inc.glsl:406 get_omni_attenuation. Zero at and beyond
// `range` (Godot's inv_range of 0 would give the same).
float OmniAttenuation(float distance, float range, float decay);

// A live omni light, in the units the bake's static light uses: the colour
// linearised and scaled by energy (light_storage.cpp:802, :842; pi cancels).
struct PointLight {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float linear[3] = {0.0f, 0.0f, 0.0f};
    float range = 0.0f;
    float decay = 1.0f;
};

// Adds `l` at `at` on a surface whose normal is unknown (a billboard
// particle): Godot's attenuation x the Lambert-wrap term (N.L + 1) / 4 taken
// at N.L = 0, a quarter.
void AddPointLight(const PointLight& l, const float at[3], float inout[3]);

// The alpha a blended quad needs in an 8-bit target to read as Godot draws it:
// Godot blends in linear HDR before the tonemap, so a quad lit far above 1 at
// alpha 0.055 still saturates (the zippo's flame). Raised by how far over 1
// its brightest channel is; unchanged at or below 1.
float HdrBlendAlpha(const float linear[3], float alpha);

// scene_forward_lights_inc.glsl:574-578: a sized light's `size_A`, which widens
// its lit side. Live because soft shadows are on: the project leaves
// positional_shadow/soft_shadow_filter_quality at Godot's default, 2 (Soft Low).
float SizeA(float lightSize, float distance);

// The Burley diffuse term, scene_forward_lights_inc.glsl:95-98, :110 and
// :124-127, multiplied by PI: Godot multiplies a light's energy by PI in
// non-physical mode (light_storage.cpp:842) and the BRDF divides by it, so the
// two cancel. N, L and V are unit vectors; A is SizeA (0 for a point light).
float BurleyNL(const float N[3], const float L[3], const float V[3], float roughness,
               float A = 0.0f);

// tonemap.glsl:214-237: tonemap_aces' exposure bias and the rational curve
// it applies per channel between its matrices. The FOG programs repeat them
// (render/shaders; SceneFog.TheFogProgramsTranscribeTheCpp pins the copies).
inline constexpr float kAcesBias = 1.8f;
inline constexpr float kAcesA = 0.0245786f, kAcesB = 0.000090537f, kAcesC = 0.983729f, kAcesD = 0.432951f,
                       kAcesE = 0.238081f;
// The curve; the caller applies the bias.
float AcesCurve(float x);

// tonemap.glsl:214 tonemap_aces.
void Aces(const float in[3], float white, float out[3]);

// tonemap.glsl:336 apply_bcs. Does not clamp: Godot's does not.
void Bcs(float c[3], float brightness, float contrast, float saturation);

// A value written to an RGBA8 target and read back: clamped, rounded to a
// byte, divided by 255. The BCS result lands in one before posterize reads it.
float StoreUnorm8(float c);

// posterization test.gdshader: floor(c * levels) / levels.
float Posterize(float c, float levels);

// The Environment of main.tscn (sub_resource 82) and the posterize ColorRect.
struct Grade {
    float white      = 1.0f;
    float brightness = 1.49f;
    float contrast   = 0.8f;
    float saturation = 0.26f;
    float levels     = 7.0f;
};

// A linear scene colour to the colour the player sees, in Godot's order
// (tonemap.glsl:455-490): ACES -> linear_to_srgb -> BCS -> the RGBA8 target
// (StoreUnorm8) -> posterize.
void GradePixel(const float linear[3], const Grade& g, float outSrgb[3]);

// BaseMaterial3D's normal map. `n` is the unit normal, `t` the tangent (xyz)
// with its handedness in w, `map` the texture's red and green, 0..1, sampled
// without sRGB (hint_normal), `depth` normal_scale. The binormal is
// cross(N, T) * sign(w) (scene_forward_clustered.glsl:300-301); z is rebuilt
// from x and y and the result mixed toward it (:888-891).
void PerturbNormal(const float n[3], const float t[4], const float map[2], float depth, float out[3]);

// The animated lights: each is baked per vertex at energy 1 as the scalar
// atten * burley_NL * shadow, one light per RGBA8 channel, stored as
// 255 * sqrt(clamp(v / 4, 0, 1)). The square root spends the 8 bits where the
// light is faint. The vertex stage decodes col * col * 4.
unsigned char EncodeDynLayer(float v);
float DecodeDynLayer(unsigned char c);
// The uniform one lit animated light adds in display space:
// mix(grey, srgb_to_linear(colour), saturation 0.26) * energy now * k. The
// grade's saturation greys light added before it (Godot's flash adds nearly
// white to the felt).
void DynUniform(const float colourSrgb[3], float energy, float k, float out[3]);

// The Environment's animated brightness b and contrast k on the baked colour.
// BCS is affine in the pre-BCS colour, and saturation is linear and keeps
// grey, so the texel graded at (Grade{}.brightness, .contrast) maps exactly:
// out = scale * out0 + offset, with
//   scale = b k / (b0 k0),  offset = 0.5 (1 - k) - scale * 0.5 (1 - k0).
// Exact up to the RGBA8 clamp and the posterize bands the bake applied at rest.
// `restBrightness`/`restContrast`: the grade the scene's pages were baked with
// (its Environment at load); main's by default.
void EnvironmentUniforms(float brightness, float contrast, float* scale, float* offset,
                         float restBrightness = Grade{}.brightness, float restContrast = Grade{}.contrast);
// The same, but while DARKENING fitted (least squares) to what Godot's
// posterize does to the 8 levels the baked texels sit on, so the fades end
// black, not grey 0.1. Brightening keeps the exact form.
void PosterizedEnvironmentUniforms(float brightness, float contrast, float* scale, float* offset,
                                   float restBrightness = Grade{}.brightness,
                                   float restContrast = Grade{}.contrast);

// The `home` camera (camera_sockets.json: fov 65.5, vertical, as Camera3D's
// KEEP_HEIGHT) rendering 540 lines (project.godot viewport_height).
inline constexpr float kHomeFovYDegrees = 65.5f;
inline constexpr float kScreenHeight = 540.0f;
// The mip level a texture of `texelsPerUnit` is sampled at from `distance`
// on a surface at `cosTheta` to the view: log2 of texels per screen pixel.
// The shipped normal maps have Y = 0 in mip 0 and their real Y from mip 1 on,
// so this decides which the bake samples (the GPU's isotropic lod).
float NormalMapLod(float texelsPerUnit, float distance, float cosTheta);

}  // namespace assets::light
