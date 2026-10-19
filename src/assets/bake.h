// Per-vertex light from the scene's OmniLights, on the CPU, at room build.
//
// The device does not draw these colours: the static light lives in the
// surface pages (tools/bake_surfaces), and bake_room puts the animated lights'
// layers, or in a fog scene the surface luminance, in a_col. This pass is
// still the build's first lighting, the luminance histogram bake_room prints,
// and the tests' check of Godot's falloff. Pure maths: no GL, no SDL.
//
// Godot 4's omni attenuation is
//     nd = (d * inv_range)^4 ; nd = max(1 - nd, 0) ; nd = nd^2
//     result = nd * pow(max(d, 0.0001), -decay)
// `decay` is OmniLight3D.attenuation, which export_scene_graph.gd writes (the
// scene's lights all use 1.0: plain inverse distance).
#pragma once

#include "assets/glb.h"
#include "assets/scene.h"
#include "core/mat4.h"

#include <cstdint>
#include <vector>

namespace assets {

struct BakeSettings {
    // Floor so unlit geometry is readable rather than pure black. The game is
    // very dark by design, so this is deliberately low.
    float ambient[3] = {0.06f, 0.06f, 0.07f};

    // Scales the summed light contribution before clamping. Now that the decay
    // term is exported and applied, this is an ordinary look control again.
    float exposure = 1.0f;

};

// One light, already resolved to world space.
struct BakedLight {
    float position[3] = {0.0f, 0.0f, 0.0f};

    // LINEAR, not sRGB. CollectLights converts on the way in, because Godot
    // does: light_storage.cpp:802 is light->color.srgb_to_linear(). Summing the
    // raw authored values renders the table's deep amber as a pale yellow with
    // roughly nine times too much blue.
    float colour[3]   = {1.0f, 1.0f, 1.0f};
    float energy      = 1.0f;
    float range       = 1.0f;

    // OmniLight3D.attenuation. 0 leaves only the geometric term.
    float attenuation = 0.0f;

    // For the surface bake: shadow_enabled, and light_size, which softens the
    // shadow and widens the lit side (SizeA).
    bool  shadow = false;
    float size = 0.0f;

    // OmniLight3D.light_cull_mask: the light reaches only instances whose
    // render layers share a bit with it (scene_forward_clustered.glsl:1924).
    uint32_t cullMask = 0xFFFFFFFFu;
};

// Godot's sRGB electro-optical transfer function, core/math/color.h:175.
//
// OmniLight3D.color is authored and stored as sRGB; Godot linearises it before
// it ever reaches a shader. This is not a cosmetic difference: it is the
// difference between the table light being a deep amber with a blue of 0.013
// and a washed-out yellow with a blue of 0.12.
float SrgbToLinear(float c);

// The vertex pass's diffuse term: Godot's diffuse_lambert_wrap at roughness 1
// (the spatial default, scene_forward_clustered.glsl:754):
//
//     max(0, (N.L + roughness) / (1 + roughness)^2)  ->  max(0, (N.L + 1) / 4)
//
// The BRDF's 1/PI is omitted on purpose: Godot multiplies light energy by PI
// in non-physical mode (light_storage.cpp:842) and the two cancel exactly.
// Plain max(N.L, 0) would be 2x too bright facing a lamp and black at grazing
// angles, where this still returns 0.25. The surface bake uses Godot's default
// Burley instead (assets/light_model.h).
float LambertWrap(float ndotl);

// One light's LINEAR RGB contribution at a point. `l.colour` must already be
// linear; CollectLights guarantees that.
void LightContribution(const BakedLight& l, const float worldPos[3],
                       const float worldNrm[3], float outRgb[3]);

// Pulls every light out of the scene with its world position resolved and its
// colour LINEARISED.
std::vector<BakedLight> CollectLights(const Scene& scene);

// Godot's omni falloff. The geometric term alone when `attenuation` is 0, which
// is Godot's behaviour at that setting.
//
// With attenuation > 0 the pow(d, -attenuation) decay is applied and the result
// is no longer bounded by 1: very close to a light it grows large, as it does
// in Godot. The caller clamps.
float OmniAttenuation(float distance, float range, float attenuation = 0.0f);

// Writes mesh.colours (4 bytes RGBA per vertex). `world` places the mesh's
// vertices into the same space as the lights' positions.
void BakeVertexLighting(Mesh& mesh, const core::Mat4& world,
                        const std::vector<BakedLight>& lights,
                        const BakeSettings& settings = BakeSettings{});

}  // namespace assets
