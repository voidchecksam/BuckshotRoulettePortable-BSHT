// The surface bake's page builder. Host tool only: xatlas and stb_dxt are
// linked here and nowhere the device sees.
//
// Every static batch and every prop is unwrapped to a unique second UV set,
// each texel is coloured with Godot's static light and the grade
// (assets/surface_bake), and the result is packed into square pages: DXT1 for
// opaque geometry, DXT5 for the blended props that need real alpha.
#pragma once

#include "assets/light_model.h"
#include "assets/probe_grid.h"
#include "assets/room.h"
#include "assets/surface_bake.h"
#include "assets/texture_pool.h"

#include <cstdint>
#include <string>
#include <array>
#include <vector>

namespace tools {

// Room pages may not grow past this. v2.2's main room needs 32.33 MB (73
// pages). The cap is the pages' own budget, not a free-memory reading: with
// main loaded the Vita still had 14.6 MB of VRAM free.
inline constexpr float kPageBudgetMegabytes = 33.0f;

// One material a script can swap onto a prop's surface.
struct PropVariantInput {
    assets::SurfaceMaterial material;   // what the texels are coloured with
    int texture = -1;                   // its albedo in SurfaceBakeInput::albedo, -1: none
    int normalTexture = -1;             // its normal map in SurfaceBakeInput::normal, -1: none
    // The match key the renderer compares the override against: the material
    // as Godot holds it (albedo_color, premultiplied emission).
    float albedo[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float emission[3] = {0.0f, 0.0f, 0.0f};
    uint32_t textureKey = 0;   // assets::TextureKeyHash of its albedo texture
};

struct SurfaceBakeInput {
    std::vector<assets::BakedLight> lights;
    // The scene profile: where the view vector and the texel tiers are
    // measured from, the Environment's grade, and its ambient light as a
    // linear colour times energy (added to every sample's light). The defaults
    // are main.tscn's: the `home` socket, sub_resource 82, no ambient.
    float eye[3] = {assets::kHomeSocket[0], assets::kHomeSocket[1], assets::kHomeSocket[2]};
    assets::light::Grade grade;
    float ambient[3] = {0.0f, 0.0f, 0.0f};
    // Environment fog baked from `eye` (assets::FogIsBaked): every MERGED
    // texel's linear colour is mixed toward `fog` (linear colour x energy) by
    // 1 - exp(-distance * fogDensity), before the grade, as Godot mixes it.
    // Props are not: they are fogged at runtime (assets/scene_fog.h). 0: no fog.
    float fogDensity = 0.0f;
    float fog[3] = {0.0f, 0.0f, 0.0f};
    // Per Room::Sources() index (batches) and per prop.
    std::vector<assets::SurfaceMaterial> sourceMaterial;
    std::vector<assets::SurfaceMaterial> propMaterial;
    // Per prop: 1 when it draws in the blended pass and keeps texture alpha.
    std::vector<uint8_t> propAlpha;
    // Per source and per prop: the node's render layers (a light reaches it
    // only through its cull mask) and whether it casts shadows. Empty means
    // every layer and casting, for callers that do not model them.
    std::vector<uint32_t> sourceLayers;
    std::vector<uint32_t> propLayers;
    std::vector<uint8_t> sourceCastsShadow;
    std::vector<uint8_t> propCastsShadow;
    // Albedo at NATIVE size, and each batch's / prop's index into it (-1: none).
    // A 1x1 entry is a colour-only material: its texel is treated as white and
    // the colour comes from the material, so it is not applied twice.
    const assets::TexturePool* albedo = nullptr;
    std::vector<int> batchTexture;
    std::vector<int> propTexture;
    // Normal maps at native size (red and green are the map's x and y, read
    // without sRGB), and each source's / prop's index into it (-1: none).
    // Empty: no normal maps. Used only where the mesh carries tangents.
    const assets::TexturePool* normal = nullptr;
    std::vector<int> sourceNormal;
    std::vector<int> propNormal;
    // Per normal-map index: its mip 1 in `normal` (-1: none). The shipped
    // RA_AS_RG maps hold Y = 0 in mip 0 and their real Y from mip 1 on; each
    // sample blends the two by the lod the `home` camera sees it at.
    std::vector<int> normalMip1;
    // Per prop (or empty for none): the materials a script can swap onto it.
    // Such props are packed onto pages of their own, and each page is baked
    // again once per variant at the same UV2.
    std::vector<std::vector<PropVariantInput>> propVariants;
    // A prop seen only under another Environment grade (the car, under the
    // ending's) is baked at that grade, on pages of its own, and carries it to
    // the runtime. Per prop: an index into `propGrades`, or -1 for the scene's
    // `grade`. Empty: every prop -1.
    std::vector<assets::light::Grade> propGrades;
    std::vector<int> propGrade;
    // Per propGrades entry: the camera its props are seen from (the ending's,
    // for the car); their texel tier and view vector are measured from it, not
    // from `eye`. Missing: `eye`.
    std::vector<std::array<float, 3>> propGradeEyes;
    // Per prop (or empty for none): 1 when it is a DETAIL surface, planned at
    // its texture's own density (assets::ChooseDetailDensity), not its tier's.
    std::vector<uint8_t> propDetail;
    int pageSize = 1024;
    float maxMegabytes = kPageBudgetMegabytes;
    int threads = 0;   // 0: one per hardware thread
    // Stop after choosing densities: report the texel budget, unwrap nothing.
    bool planOnly = false;
};

struct SurfacePage {
    int size = 0;
    bool alpha = false;             // DXT5 when true, DXT1 otherwise
    int grade = -1;                 // SurfaceBakeInput::propGrades index; -1: the scene's
    std::vector<uint8_t> rgba;      // the graded texels, RGBA8 (for inspection)
    std::vector<uint8_t> encoded;   // what ships: DXT1 or DXT5 blocks
};

// Where one output vertex was cut from: its albedo texel (sRGB, white when
// untextured) and its material, as ColourFace shades the texels around it.
struct VertexSurface {
    float texel[3] = {1.0f, 1.0f, 1.0f};
    assets::SurfaceMaterial material;
};

struct SurfaceBakeResult {
    std::vector<SurfacePage> pages;
    // The geometry rebuilt around the pages: one page per batch/prop, UV2 in
    // [0,1], at most 65535 vertices each. A prop whose charts landed on two
    // pages becomes two props on the same node.
    std::vector<assets::RoomBatch> batches;
    std::vector<assets::RoomProp> props;
    // Bake only, parallel to batches / props and their vertices: what each
    // vertex was cut from (a page's batch merges many materials). The fog
    // luminance (BakeFogLuminance) needs it after the bake.
    std::vector<std::vector<VertexSurface>> batchSurfaces;
    std::vector<std::vector<VertexSurface>> propSurfaces;
    std::size_t encodedBytes = 0;
    double texelsByTier[3] = {0.0, 0.0, 0.0};   // batches, by tier
    double propTexels[3] = {0.0, 0.0, 0.0};     // props, by the tier they sit in
    int propSplits = 0;
    int unplacedFaces = 0;
    std::string error;   // non-empty on failure
};

bool BakeSurfaces(const assets::Room& room, const SurfaceBakeInput& in, SurfaceBakeResult& out);

// The light probe grid: points `step` apart covering [lo, hi], each the mean
// linear static light over the six axis normals, with shadows from the static
// room (as a prop's own texels take them). `layers` is the render layer the
// probed props sit on: a light whose cull mask misses it does not count.
// The animated lights: up to four, one per RGBA8 channel in order, each at
// energy 1 and white, as atten * burley_NL * shadow at every vertex of `mesh`
// placed at `world`, encoded with light::EncodeDynLayer. A light whose cull
// mask misses `layers` leaves its channel 0; so does a channel with no light.
// Replaces the mesh's colours.
void BakeDynLayers(const std::vector<assets::BakedLight>& dyn, const assets::LightBvh& bvh,
                   const core::Mat4& world, uint32_t layers, assets::Mesh& mesh);

// Fog scenes: the FOG program fogs per vertex, so a triangle spanning much of
// the fog draws all of it at its corners' (heaven's floor is two triangles 150
// units across). Splits every edge longer than `maxEdge` in the world (`world`
// places the mesh) at a midpoint shared by both its triangles, and
// re-triangulates by how many of a triangle's edges split, so no crack opens,
// until none is longer or the mesh would pass 65535 vertices. Attributes
// (normals, UVs, page UVs, colours, `surfaces` when given) are interpolated; a
// skinned mesh is left alone (and reports nothing).
struct FogTessellation {
    int added = 0;             // triangles added
    bool capped = false;       // stopped at 65535 vertices: long edges may remain
    float longestEdge = 0.0f;  // the longest world edge left
};
FogTessellation TessellateForFog(assets::Mesh& mesh, std::vector<VertexSurface>* surfaces, const core::Mat4& world,
                                 float maxEdge);

// Fog scenes (assets/scene_fog.h): each vertex's surface luminance, linear,
// into colours[v * 4 + 3] as assets::EncodeFogLuminance; RGB stay 0. The
// surface is `surfaces[v]` (texel and material, as the texels are shaded; null
// or short: white) under `lights` unshadowed plus `ambient`. Replaces the
// mesh's colours.
void BakeFogLuminance(const std::vector<assets::BakedLight>& lights, const core::Mat4& world, uint32_t layers,
                      const float ambient[3], const std::vector<VertexSurface>* surfaces, assets::Mesh& mesh);
// The same under the MEAN light over `poses` (RoomProp::meanPoses: a prop lit
// with the mean of the poses it passes through, as its texels are).
void BakeFogLuminance(const std::vector<assets::BakedLight>& lights, const std::vector<core::Mat4>& poses,
                      uint32_t layers, const float ambient[3], const std::vector<VertexSurface>* surfaces,
                      assets::Mesh& mesh);

assets::ProbeGrid BakeProbes(const assets::Room& room, const SurfaceBakeInput& in, const float lo[3],
                             const float hi[3], float step, uint32_t layers);

// Writes a page's graded texels as a binary PPM (alpha dropped) for inspection.
bool WritePagePpm(const SurfacePage& page, const std::string& path);

}  // namespace tools
