// The colour of one baked texel.
//
// Every surface is baked offline to the colour the player sees: the native
// albedo times Godot's static light (with shadows), through Godot's whole
// output chain. Headless; the page builder in bake_room calls this per texel.
#pragma once

#include "assets/bake.h"
#include "assets/light_bvh.h"
#include "assets/light_model.h"

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace assets {

// One surface's BaseMaterial3D parameters, as exported.
struct SurfaceMaterial {
    float albedo[3] = {1.0f, 1.0f, 1.0f};    // albedo_color, sRGB as authored
    float roughness = 1.0f;
    float metallic = 0.0f;
    float emission[3] = {0.0f, 0.0f, 0.0f};  // LINEAR: srgb_to_linear(colour) * energy
    bool  unshaded = false;                  // shading_mode 0: no lighting at all
    float alpha = 1.0f;                      // albedo_color.a (blended surfaces)
    float normalScale = 1.0f;                // normal_scale, when a normal map is bound
};

// Linear light arriving at (pos, nrm) from `lights`, with shadows for the
// lights that cast them. V points at `eye` (the `home` socket).
// A shadow ray starts 1e-3 along the normal; a sized light averages 16 fixed
// points on a sphere of its radius.
// `layers` are the surface's render layers: a light whose cull mask misses
// them does not light it.
void StaticLight(const std::vector<BakedLight>& lights, const LightBvh& bvh,
                 const float pos[3], const float nrm[3], const float eye[3],
                 float roughness, float outLinear[3], uint32_t layers = 0xFFFFFFFFu);

// The LINEAR scene colour of one sample, before the grade: what Godot's
// renderer has in its HDR buffer. Texels average these over their subsamples
// and grade once.
void SceneLinear(const float albedoSrgbTexel[3], const SurfaceMaterial& m,
                 const float lightLinear[3], float outLinear[3]);

// The final displayed sRGB colour of one texel. `albedoSrgbTexel` is the
// texture's sample (white when untextured); `lightLinear` is StaticLight's.
void TexelColour(const float albedoSrgbTexel[3], const SurfaceMaterial& m,
                 const float lightLinear[3], const light::Grade& g, float outSrgb[3]);

// ---- the page builder's headless pieces ----

// The `home` camera socket (assets/extracted/camera_sockets.json): tiers are
// measured from where the player sits.
inline constexpr float kHomeSocket[3] = {-13.59f, 7.323f, 0.024f};

// 0: <= 25 units from home (the table, the dealer); 1: <= 40; 2: the rest.
int Tier(float distanceFromHome);

// Texels per world unit for a surface whose albedo has `native` texels per
// unit (0 when untextured): the texture's own detail, clamped to the tier.
float ChooseDensity(float native, int tier);

// A detail surface (a screen's icons: the health counter, the round indicator,
// the CRT) keeps its texture's own detail up to this cap, whatever its tier.
// At its tier's 32 texels per unit the counter's icons were ~20 texels across.
inline constexpr float kDetailDensityCap = 256.0f;
float ChooseDetailDensity(float native);

// True for a material texture key the SCENE authored ("inline_textures/misc_..."),
// false for one of a model's own ("inline_textures/models_...") or none. A
// scene texture replaces the model's on that surface (main.tscn's
// surface_material_override), so the bake must sample it, not the GLB's.
bool IsSceneAuthoredTexture(std::string_view key);

// Calls `sample(x, y, bary)` for every 2x2 subsample of every texel of a
// `size` x `size` page that lies inside the triangle (edges inclusive).
// `uv` is in texels. Barycentrics weight the three vertices in order.
void RasteriseTriangle(const float uv[3][2], int size,
                       const std::function<void(int, int, const float*)>& sample);

// Places rectangles (texels, each already a multiple of 4) on square pages:
// tallest first onto shelves, 4-texel aligned so a DXT1 block never holds two
// meshes. `pages` returns how many pages were opened, or -1 when a rect is
// larger than the page (nothing is placed).
struct PackRect { int w = 0; int h = 0; };
struct Placement { int page = -1; int x = 0; int y = 0; };
std::vector<Placement> ShelfPack(const std::vector<PackRect>& rects, int pageSize, int& pages);

// Grows the filled texels (`mask` 1) outward by `passes` texels, each empty
// texel taking the first filled 8-neighbour, so bilinear filtering at a chart
// edge never reads unbaked space. `texels` holds `channels` bytes per texel.
void Dilate(std::vector<uint8_t>& texels, std::vector<uint8_t>& mask, int size, int passes,
            int channels = 3);

}  // namespace assets
