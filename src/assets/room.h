// Assembles the room from the scene graph and the model library: Scene says
// where each node goes, MeshIndex which mesh it draws, Glb the geometry.
//
// Headless: building the room is loading, lighting and merging, all data work,
// so it is unit-tested without a window. render/ only uploads and draws it.
//
// Static geometry goes to world space at build time, which is what lets
// separate nodes merge into one buffer and one draw. Anything that moves stays
// a prop in model space (RoomProp).
#pragma once

#include "assets/animated_nodes.h"
#include "assets/bake.h"
#include "assets/inline_meshes.h"
#include "assets/texture_pool.h"
#include "assets/node_table.h"
#include "assets/glb.h"
#include "assets/mesh_index.h"
#include "assets/probe_grid.h"
#include "assets/scene.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace assets {

struct RoomStats {
    int nodesConsidered   = 0;
    int nodesDrawn        = 0;
    int inlinePrimitives  = 0;   // no extracted counterpart
    int inlineDrawn       = 0;   // ...of which the fallback supplied geometry
    int inlineMissing     = 0;   // ...and of which it supplied none

    // Every considered node lands in exactly one bucket. Without inlineMissing
    // the 76 inline primitives fell out of the sum entirely whenever the
    // fallback library was absent, so "nothing was silently dropped" could not
    // actually be checked.
    int Accounted() const {
        return nodesDrawn + outsideFilter + invisible + unresolved + inlineMissing;
    }
    int unresolved        = 0;   // resolved to nothing at all
    int invisible         = 0;   // node marked not visible
    int outsideFilter     = 0;   // skipped because the area filter excluded them
    int props             = 0;   // held out of the merge so animation can move them
    int primitivesMerged  = 0;
    int batches           = 0;
    int triangles         = 0;
    int modelsLoaded      = 0;
};

// One draw call's worth of geometry, already in world space and already lit.
struct RoomBatch {
    Mesh mesh;
    int  materialIndex = -1;
    std::string sourceGlb;   // the texture join key, with materialIndex

    // Index into the room's TexturePool, or -1 for untextured. In a shipped
    // room it is the batch's surface page; during a build, the albedo of its
    // one material.
    int texture = -1;

    // Index of the baked surface page this batch samples, or -1. bake_room
    // copies it into `texture`, which is what the room file stores.
    int page = -1;

    // World-space AABB, computed as geometry is merged in. Stored in the baked
    // file so area culling on device needs no pass over the vertices.
    float boundsMin[3] = { 1e30f,  1e30f,  1e30f};
    float boundsMax[3] = {-1e30f, -1e30f, -1e30f};

    bool HasBounds() const { return boundsMin[0] <= boundsMax[0]; }

    // Bake only, never written to the file: per triangle, an index into
    // Room::Sources() naming the node and surface it came from, so the surface
    // bake can look up that surface's material in the runtime export.
    std::vector<int32_t> triSource;
};

// Where a merged triangle came from (bake-only).
struct SurfaceSource {
    std::string nodePath;
    int surface = 0;
};

// One material a script can put on a prop's surface, baked to its own page.
// The key is the material as Godot holds it (its albedo colour and its
// premultiplied emission, both straight from the export), so the renderer
// matches the script's override without knowing which script.
struct PropVariant {
    float albedo[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float emission[3] = {0.0f, 0.0f, 0.0f};
    // TextureKeyHash of the material's albedo_texture key, 0 for none
    // (EndlessMode and CrtIcon swap only the texture).
    uint32_t textureKey = 0;
    int texture = -1;   // the variant's page, as an index into the room's pool
    int page = -1;      // bake only: the page index in the bake's own output
};

// The texture of the variant baked for the material `albedo` + `emission`, or
// -1 when none was. Shared by RoomProp and the renderer's own prop record.
int FindVariantTexture(const std::vector<PropVariant>& variants, const float albedo[4],
                       const float emission[3], uint32_t textureKey);

// FNV-1a of a material's albedo_texture key ("inline_textures/x.png"); 0 for "".
uint32_t TextureKeyHash(std::string_view key);

// A node that animation or a script must be able to move, so it is not merged.
//
// Geometry stays in model space: Godot animates a node's local transform, so
// the world transform is parentWorld * animatedLocal. Its texels are lit at
// bakeWorld; a prop that moves is scaled by the probe ratio at runtime
// (assets/probe_grid.h).
struct RoomProp {
    Mesh mesh;                   // model space
    std::string nodePath;
    std::string sourceGlb;
    int  materialIndex = -1;

    // Index into the room's TexturePool, or -1. Same join as RoomBatch.
    int  texture = -1;

    // Index of the baked surface page this prop samples, or -1.
    int  page = -1;

    // Whether this prop is on screen when the scene loads.
    //
    // A prop hidden at load is kept, because "hidden now" and "hidden forever"
    // differ: the dealer's hands, the shells and the item models all start
    // hidden and are shown when they are used. Merged static geometry has no
    // such handle and is still filtered at bake time.
    bool visibleAtLoad = true;

    // The node's own flag, without its ancestors. visibleAtLoad above is the
    // whole chain; the runtime needs the two apart so a script can show a
    // parent and have this appear.
    bool ownVisible = true;

    // Index into Room::NodeTable(). The world transform comes from walking that
    // hierarchy, not from a stored matrix: animation often moves an ancestor
    // rather than the prop's own node.
    int  node = -1;

    float boundsMin[3] = { 1e30f,  1e30f,  1e30f};
    float boundsMax[3] = {-1e30f, -1e30f, -1e30f};

    // The primitive's index within its glTF mesh, which is the Godot surface a
    // set_surface_override_material call targets (ShellClass recolours 1).
    int surface = 0;

    // Non-empty for a TEMPLATE prop: geometry for a PackedScene's mesh, drawn
    // for every runtime clone whose MeshInstance3D carries this key
    // ("instances/shell.tscn:shell parent"). A template prop has no node path,
    // no node and no bounds (clones move, so it is never culled).
    std::string templateKey;
    bool IsTemplate() const { return !templateKey.empty(); }

    // Bake only, never written to the file: where the prop is lit (its node's
    // world transform at rest, a template's lighting pose, or where its reveal
    // animation leaves it; bake_room).
    core::Mat4 bakeWorld;
    // Bake only: where the prop casts its shadow onto the room from, when that
    // is not bakeWorld: its rest pose, once bake_room has moved bakeWorld to a
    // reveal pose. The item compartments are lit raised but rest under the
    // table, and raised they would shadow the felt.
    core::Mat4 castWorld;
    bool hasCastWorld = false;
    // Bake only: a second pose the prop is seen in (its rest pose, when
    // bakeWorld is a reveal pose). Each texel is lit in whichever of the two
    // poses lights it more.
    core::Mat4 altWorld;
    bool hasAltWorld = false;
    // Bake only: more poses the prop is seen in (the signature machine's lid
    // open while the player signs, shut as it leaves). Each texel takes the
    // brightest of bakeWorld, altWorld and these.
    std::vector<core::Mat4> extraPoses;
    // Bake only: poses a prop passes through on a loop. When set, each texel is
    // lit with the mean of their light (Godot's average over the motion: the
    // menu's shell waterfalls falling past their lamps), and bakeWorld /
    // altWorld do not light it.
    std::vector<core::Mat4> meanPoses;

    // Materials a script can swap onto this surface, each with its own page
    // at the same UV2 (shell live/blank/brass, blinker on/off).
    std::vector<PropVariant> variants;

    // The page baked for the material `albedo` + `emission`, or -1 when none
    // was: the caller keeps the page at load.
    int VariantTexture(const float albedo[4], const float emission[3], uint32_t textureKey) const;

    bool HasBounds() const { return boundsMin[0] <= boundsMax[0]; }

    // Where the prop's origin was when its texels were lit (the translation of
    // bakeWorld), written to the file (v10) so the renderer can take the probe
    // ratio against it. False: the prop is never rescaled.
    bool  hasBakePivot = false;
    float bakePivot[3] = {0.0f, 0.0f, 0.0f};

    // The Environment grade its texels were baked at, when not the scene's:
    // the car is seen only under the ending's. The renderer grades the prop
    // relative to it. 0: the scene's grade.
    float bakeBrightness = 0.0f;
    float bakeContrast = 0.0f;
    // Both factors: brightness 0 would divide by zero in the runtime's re-grade.
    bool HasOwnGrade() const { return bakeBrightness > 0.0f && bakeContrast > 0.0f; }
};

// Restricts the build to nodes near a point.
//
// The scene holds every location at once: lobby, backroom, restroom, heaven,
// vehicle. The shipping bake keeps the whole world; tests and bake_room's
// radius option build only the play area, which sits near the origin with
// every other location 60-320 units away.
//
// radius <= 0 disables filtering.
struct RoomFilter {
    float centre[3] = {0.0f, 0.0f, 0.0f};
    float radius    = 0.0f;

    bool Enabled() const { return radius > 0.0f; }
    bool Contains(const float p[3]) const {
        if (!Enabled()) return true;
        const float dx = p[0] - centre[0];
        const float dy = p[1] - centre[1];
        const float dz = p[2] - centre[2];
        return dx * dx + dy * dy + dz * dz <= radius * radius;
    }
};

struct RoomBuildOptions {
    // Directory holding the .glb files.
    std::string modelDir;

    // Which part of the world to build. Defaults to everything.
    RoomFilter filter;

    // Nodes listed here are emitted as individual props rather than merged.
    // Without it, animated geometry has no per-node identity and cannot move.
    const AnimatedNodeSet* animated = nullptr;

    // Node paths Godot does not draw when the scene loads, by the renderer's
    // own rule (render::PropIsDrawn) on the live export: not visible in tree, on
    // no layer the camera sees, or fully transparent. scene_graph.json cannot
    // tell the last two apart from ordinary geometry, and merged geometry has no
    // handle the renderer can hide: `dealer head crushed` (layers 0) and
    // `bracket selection 3d_waiver` (transparency 1) would draw over everything.
    // Listed nodes are always props.
    const std::unordered_set<std::string>* notDrawnAtLoad = nullptr;

    // Node paths whose material or transform a translated script writes at
    // runtime, from the scripts on the live export (BlinkerBranch flashes its
    // parent's surface 0). Merged geometry has no per-node handle, so such a
    // node could never change colour. Listed nodes are always props.
    const std::unordered_set<std::string>* scriptDriven = nullptr;

    // Geometry for nodes whose mesh has no name and so is in no .glb. Without
    // it those nodes are counted and never drawn.
    const InlineMeshLibrary* inlineMeshes = nullptr;

    BakeSettings bake;

    // Skip nodes flagged invisible in the scene. The room has plenty of hidden
    // helper geometry, and drawing it wastes fill rate, which is the budget.
    bool skipInvisible = true;

    // Keep mesh nodes hidden at load as props, drawn only once something shows
    // them, instead of dropping them. Scripts toggle `visible` on geometry that
    // has no animation: the health counter's digits, shells, the waiver's
    // parts. A hidden prop costs memory and no fill. Off by default so a room
    // built for a test stays small; the shipping bake turns it on.
    bool keepHiddenAsProps = false;

    // A batch stops accepting geometry at this many vertices. GLES2 has no
    // 32-bit index path, so a merged buffer past 65535 vertices cannot be
    // indexed at all: a format limit, not a tuning knob.
    int maxVerticesPerBatch = 65535;
};

class Room {
public:
    bool Build(const Scene& scene, const MeshIndex& index,
               const RoomBuildOptions& options);

    const std::vector<RoomBatch>& Batches() const { return batches_; }
    const std::vector<RoomProp>&  Props()   const { return props_; }
    const std::vector<SurfaceSource>& Sources() const { return sources_; }

    // Mutable, so the offline texture bake can fill in each batch's texture
    // index after the geometry is merged. Nothing at runtime uses these.
    std::vector<RoomBatch>& Batches() { return batches_; }
    std::vector<RoomProp>&  Props()   { return props_; }

    // The room's textures. Empty until the baker fills it: assets/ links no
    // image decoder, and must not.
    const TexturePool& Textures() const { return textures_; }
    TexturePool&       Textures()       { return textures_; }
    const NodeTable&              Nodes()   const { return nodes_; }
    NodeTable&                    MutableNodes()   { return nodes_; }
    const RoomStats& Stats() const { return stats_; }

    void AdoptProps(std::vector<RoomProp>&& props);

    // Frees every batch's and prop's vertex and index arrays, keeping the props,
    // batches and their names, materials and nodes. Call once the room is on
    // the GPU: the renderer keeps its own copy of what it still needs (a skinned
    // prop's bind mesh), and on the Vita the table's CPU geometry is tens of MB
    // of a fixed 128 MB heap.
    void ReleaseGeometry();

    // Appends template props for `meshName`: one per primitive, geometry in
    // model space, vertex light baked with the mesh placed at `lightingWorld`.
    // Returns how many were added; 0 when the mesh does not resolve or its
    // model will not load. Stats gain the props and triangles; the build's node
    // accounting is left alone.
    // `surfaceTextures[i]`, when non-empty, is the texture key ("inline_textures/
    // x.png") the scene's material draws surface i with. It wins over the
    // .glb's own material, as the scene's material does in Godot.
    int AddTemplateProps(const std::string& templateKey, const std::string& meshName,
                         const MeshIndex& index, const core::Mat4& lightingWorld,
                         const Scene& scene, const RoomBuildOptions& options,
                         const std::vector<std::string>& surfaceTextures = {});

    // Populates the room directly, for the baked-file reader. Stats are
    // recomputed from the batches so a loaded room reports the same batch and
    // triangle counts a built one does.
    void AdoptBatches(std::vector<RoomBatch>&& batches);
    // Bake-only: the merged triangles' sources (RoomBatch::triSource indexes it).
    void AdoptSources(std::vector<SurfaceSource>&& sources) { sources_ = std::move(sources); }

    // The static light over the table: a moved prop is scaled by
    // probe(pivot now) / probe(bakePivot). Empty: no scaling.
    const ProbeGrid& Probes() const { return probes_; }
    void SetProbes(ProbeGrid grid) { probes_ = std::move(grid); }

private:
    void RecomputeStats();

public:

private:
    std::vector<RoomBatch> batches_;
    std::vector<RoomProp>  props_;
    std::vector<SurfaceSource> sources_;   // bake-only; see RoomBatch::triSource
    TexturePool            textures_;
    NodeTable              nodes_;
    ProbeGrid              probes_;
    RoomStats stats_;
};

}  // namespace assets
