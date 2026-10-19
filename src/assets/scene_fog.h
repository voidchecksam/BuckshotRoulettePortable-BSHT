// Where a scene's Environment fog is drawn. One set of rules, read by the bake
// (tools/bake_room) and the runtime (GameApp, SceneRenderer), so a surface is
// fogged exactly once.
//
// Godot mixes fog into the linear colour, before the tonemap and the grade
// (scene_forward_clustered.glsl:2233, then tonemap.glsl). The pages hold the
// graded colour, so a mix of the texel with a weight of (1 - T) is not Godot's:
// in the menu's black fog it keeps T of a shell where Godot keeps
// G(T x) / G(x), about twice as much.
//
//  - Static, merged geometry in a scene whose camera never moves (the menu:
//    FogIsBaked) is fogged per texel at bake time, in linear light, exact.
//  - Everything else in a fog scene (every prop, since the menu's shells fall
//    through five units of depth, and all of heaven, whose camera flies) is
//    fogged per vertex at runtime: each vertex carries its surface's linear
//    luminance x in a_col.a (EncodeFogLuminance), and the FOG program mixes
//    the graded texel toward the graded fog by FogWeight, the weight that
//    makes the grade of a grey x come out as the grade of Godot's mix
//    T x + (1 - T) f. The shaders transcribe FogGrey and FogWeight line for
//    line (render/shaders/surface.h, surface_batched.h).
#pragma once

#include "assets/light_model.h"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace assets {

inline bool FogIsBaked(std::string_view scene) { return scene == "menu"; }

// The longest triangle edge a runtime-fogged scene keeps (tools::TessellateForFog):
// the FOG program fogs per vertex, so an edge may span 5% of the transmission,
// within [2, 16] units. 0 without fog.
inline float FogMaxEdge(float density) {
    if (!(density > 0.0f)) return 0.0f;
    const float edge = 0.05f / density;
    return edge < 2.0f ? 2.0f : (edge > 16.0f ? 16.0f : edge);
}

// log2(x) over 20 octaves from 2^-10 into a byte: x from 0.001 to 1024, ~5.6%
// a step. The FOG vertex shader decodes it as exp2(a * 20.0 - 10.0).
inline constexpr float kFogLuminanceOctaves = 20.0f;
inline constexpr float kFogLuminanceFloor = -10.0f;   // log2 of the darkest

inline uint8_t EncodeFogLuminance(float x) {
    if (!(x > 0.0f)) return 0;
    const float a = (std::log2(x) - kFogLuminanceFloor) / kFogLuminanceOctaves;
    if (a <= 0.0f) return 0;
    return static_cast<uint8_t>(a >= 1.0f ? 255.0f : a * 255.0f + 0.5f);
}

inline float DecodeFogLuminance(uint8_t c) {
    return std::exp2(static_cast<float>(c) / 255.0f * kFogLuminanceOctaves + kFogLuminanceFloor);
}

// The scene's grade seen on a grey: ACES keeps a grey grey (its matrices'
// rows sum to 1), saturation leaves it alone. What the FOG program needs.
struct FogCurve {
    float whiteInv = 1.0f;     // 1 / AcesCurve(1.8 * white)
    float brightness = 1.0f;
    float contrast = 1.0f;
};
FogCurve FogCurveOf(const light::Grade& g);

// A grey of linear luminance `y` as the player sees it, before posterize:
// light::GradePixel's chain, clamped as the RGBA8 target clamps.
float FogGrey(float y, const FogCurve& c);

// The FOG program's v_fog: how far to mix the graded surface (linear
// luminance `surface`) toward the fog as drawn (linear luminance `fog`, its
// drawn grey `fogGrey`) at transmission `t` = exp(-distance * density), so the result is
// FogGrey(t * surface + (1 - t) * fog). Where surface and fog grade alike any
// weight shows the same; it falls back to 1 - t.
float FogWeight(float t, float surface, float fog, float fogGrey, const FogCurve& c);
// Below this span between the surface's grey and the fog's, FogWeight falls back.
inline constexpr float kFogSpanFloor = 0.002f;

// Everything the FOG program takes about the fog besides its density and
// graded colour.
struct FogMix {
    FogCurve curve;
    float luminance = 0.0f;   // the fog colour's linear luminance
    float grey = 0.0f;        // the grey of the colour the program mixes toward (graded, posterized)
};

}  // namespace assets
