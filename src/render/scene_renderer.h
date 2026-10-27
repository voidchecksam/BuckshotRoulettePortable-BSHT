// Uploads an assembled Room and draws it.
//
// Thin on purpose: the load, bake and merge work happens headlessly in
// assets::Room, so this owns nothing but GPU buffers and the draw loop, and
// the merge and the transforms are unit-tested without a window.
//
// Props are drawn from the godot:: tree: each is bound once to its
// GeometryInstance3D, and every frame reads that node's global transform and
// the one draw rule in render/prop_visibility.h. Nothing pushes visibility or
// transforms into the renderer.
#pragma once

#include "core/profiler.h"

#include <array>
#include <memory>
#include <vector>

#include "godot/skeleton.h"

#include "assets/probe_grid.h"

#include "assets/room.h"
#include "assets/scene_fog.h"
#include "core/frustum.h"
#include "godot/scene_tree.h"
#include "render/camera.h"
#include "render/mesh_gpu.h"
#include "render/prop_chunk_gpu.h"
#include "render/shaders/surface_batched.h"
#include "render/sprite_batch.h"
#include "render/shader.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace render {

class SceneRenderer {
public:
    bool Upload(const assets::Room& room);

    // Compiles the PSX shader pair. Separate from Upload so a shader edit can
    // be retried without re-uploading the whole room.
    bool BuildShaders();

    // Resolves every prop to its godot:: node, once, after Upload. Returns how
    // many props have no node; each is logged by name, because a prop without
    // a node is never drawn.
    int BindTree(const godot::SceneTree& tree);

    void Draw(const Camera& camera);

    // Room textures (the baked pages) that reached the GPU, and how many there
    // were: a page that fails leaves its surfaces WHITE, so the report says it.
    int TexturesUploaded() const { return texturesUploaded_; }
    int TexturesTotal() const { return static_cast<int>(textures_.size()); }
    int BatchCount() const { return static_cast<int>(batches_.size()); }
    int TriangleCount() const;

    // What the LAST Draw() actually submitted, after culling. The distinction
    // matters: uploaded geometry costs memory, drawn geometry costs frame time,
    // and on this hardware only the second is the budget.
    int DrawnBatches() const { return drawnBatches_; }
    int DrawnTriangles() const { return drawnTriangles_; }
    int TextureCount() const { return static_cast<int>(textures_.size()); }
    int TextureBinds() const { return textureBinds_; }
    int HiddenProps() const { return hiddenProps_; }

    // Debug override, by the prop's scene node path. Returns false when no
    // prop matches, so a typo is not silently ignored. It wins over the scene
    // state, which makes it useful for diagnosis and wrong for gameplay: game
    // code changes the godot:: tree instead.
    bool OverridePropVisible(const std::string& nodePath, bool visible);

    void SetCullingEnabled(bool on) { cull_ = on; }
    bool CullingEnabled() const { return cull_; }

    // Virtual raster the vertex snap quantises to (320x240 is the PS1's); 0 is
    // no snap. Off by default: main.tscn references no PSX shader, and its
    // StandardMaterial3D geometry neither snaps nor maps affinely. Turned on,
    // both z-fight the health counter's decals against its screen and slide
    // the round indicator's glow out of its ring.
    void SetSnap(float x, float y) { snapX_ = x; snapY_ = y; }
    // Affine (PS1) texture mapping, or the interpolator's perspective-correct UV.
    void SetAffine(bool on) { affine_ = on; }

    // The Environment's animated brightness, applied as the exact affine form
    // of BCS on the baked (graded) colour; (1, 0) at rest. SetLiveGrade gives
    // the live brightness and contrast, for props baked at their own grade
    // (RoomProp::HasOwnGrade: the car, at the ending's). `on` false: there is
    // no WorldEnvironment and those props take the scene's grade.
    void SetLiveGrade(bool on, float brightness, float contrast) {
        liveGrade_ = on;
        liveBrightness_ = brightness;
        liveContrast_ = contrast;
    }
    // Albedos whose variant pages skip a brightening grade (adrenaline,
    // revival): the shells' live and blank colours stay readable.
    void SetHeldColours(std::vector<std::array<float, 3>> colours) { heldColours_ = std::move(colours); }
    void SetEnvironmentGrade(float scale, float offset) {
        envScale_ = scale;
        envOffset_ = offset;
    }

    // CPU skinning times itself into this profiler (null: off).
    void SetProfiler(core::Profiler* p) { profiler_ = p; }

    // Prop batching. Wanted: set before BuildShaders/Upload (off draws every
    // prop alone, for A/B frames). On only if every batched program compiled;
    // PropBatchingOff() says why when it is not. GameApp decides it
    // (app/device_features.h); `whyOff` is logged as the reason.
    void SetPropBatching(bool wanted, std::string whyOff = {}) { batchingWanted_ = wanted; batchingWhyOff_ = std::move(whyOff); }
    bool PropBatching() const { return batching_; }
    const std::string& PropBatchingOff() const { return batchingOff_; }
    int PropChunks() const { return static_cast<int>(chunks_.size()); }

    // GPU skinning (render/skin_gpu_plan.h). Wanted: set before BuildShaders.
    // On only if the three skinned programs compiled; off, or for a prop with
    // more binds than the palette holds, the CPU skins.
    void SetGpuSkinning(bool wanted, std::string whyOff = {}) { gpuSkinWanted_ = wanted; gpuSkinWhyOff_ = std::move(whyOff); }
    bool GpuSkinning() const { return gpuSkin_; }
    const std::string& GpuSkinningOff() const { return gpuSkinOff_; }

    // What the last Draw's props were, for deciding and checking prop
    // batching. `mergeable`: drawn opaque, not skinned, outside the moving
    // light, unscaled (no tint, probe ratio 1) and where it was last frame,
    // so props that could share a draw per page (base or variant).
    struct PropCensus {
        int drawn = 0;
        int still = 0;
        int mergeable = 0;
        int mergeablePages = 0;
        int variants = 0;
        int scaled = 0;
        int blended = 0;
        int skinned = 0;
        int moveScope = 0;
        int clones = 0;
        int batched = 0;        // drawn as part of a chunk's draw
        int batchedDraws = 0;   // the chunk draws that carried them
        int listRebuilds = 0;   // chunk index lists rebuilt this frame
    };
    const PropCensus& Census() const { return census_; }
    // Every prop's vertices, uploaded (the scale prop batching would copy).
    std::size_t PropVertices() const { return propVertices_; }
    // Prop vertices carrying baked a_col (animated-light layers, or the fog
    // luminance); the rest upload 255s (mesh_gpu.cpp AppendVertices).
    std::size_t PropColouredVertices() const { return propColoured_; }
    // Hardware probe variants (debug/hw_probe): textures off (the page fetch's
    // cost) and props off (the draw-call cost). Both off in play.
    void SetProbeVariant(bool noTextures, bool batchesOnly) {
        probeNoTextures_ = noTextures;
        probeBatchesOnly_ = batchesOnly;
    }
    // Environment fog. `colour` and `clear` are graded display colours. With
    // fog `on`, Draw clears to `clear` (the background is the fog) and draws
    // props with the FOG program; `shade` also fogs the merged batches (off
    // where the bake fogged them, assets/scene_fog.h).
    void SetFog(bool on, bool shade, float density, const float colour[3], const float clear[3],
                const assets::FogMix& mix) {
        fogOn_ = on;
        fogShade_ = shade;
        fogDensity_ = density;
        fogMix_ = mix;
        for (int i = 0; i < 3; ++i) {
            fogColour_[i] = colour[i];
            fogClear_[i] = clear[i];
        }
    }
    bool FogActive() const { return active_ == &surfaceFog_; }
    // The moving light (assets::kMovingLight), per vertex, while `on`.
    void SetMovingLight(bool on, const float pos[3], float range, const float colour[3]) {
        moveOn_ = on;
        moveRange_ = range;
        for (int i = 0; i < 3; ++i) {
            movePos_[i] = pos[i];
            moveColour_[i] = colour[i];
        }
    }
    bool DynProgramReady() const { return surfaceDyn_.Valid(); }
    bool DynActive() const { return active_ == &surfaceDyn_; }

    // The four animated lights' u_dynN (linear colour x energy now x gain).
    // The DYN program is bound only while `anyLit`.
    void SetDynLights(const float dyn[4][3], bool anyLit) {
        for (int i = 0; i < 4; ++i)
            for (int c = 0; c < 3; ++c) dyn_[i][c] = dyn[i][c];
        dynLit_ = anyLit;
    }

private:
    core::Profiler* profiler_ = nullptr;
    // Per-batch world AABB, kept alongside the GPU buffers so culling needs no
    // access to the CPU-side mesh after upload.
    struct Bounds {
        float lo[3];
        float hi[3];
    };

    // A prop keeps its own GPU buffers, its node path and the godot:: node that
    // holds its live state.
    struct Prop {
        MeshGpu     gpu;
        int         texture = -1;

        // Debug overlay only: -1 none, 0 force hidden, 1 force visible. Wins
        // over the scene state, so the device can distinguish "hidden" from
        // "missing" without another build.
        int         forceVisible = -1;
        std::string nodePath;
        Bounds      bounds;               // MODEL space; see LoadRoom
        bool        hasModelBounds = false;
        const godot::GeometryInstance3D* node = nullptr;   // resolved by BindTree
        int         surface = 0;          // the Godot surface this primitive is
        std::string templateKey;          // non-empty: drawn for PackedScene clones
        std::vector<assets::PropVariant> variants;   // a page per swappable material
        bool        hasBakePivot = false; // scaled by the probe ratio when moved
        bool        movingLightScope = false;   // lit by the moving light (assets::InMovingLightScope)
        bool        offstage = false;   // dark beyond the probes (assets/offstage.h)
        // A skinned prop keeps its bind-pose mesh and re-skins when its
        // skeleton's pose moves.
        struct Skin {
            assets::Mesh bind;                       // positions, joints, weights
            const godot::Skeleton3D* skeleton = nullptr;
            std::vector<int> bindBone;               // per bind: the skeleton's bone, -1 none
            std::vector<core::Mat4> bindPose;
            std::vector<core::Mat4> palette;
            std::vector<float> skinned;
            uint32_t version = ~0u;          // the pose the vertices were skinned to (CPU path)
            uint32_t paletteVersion = ~0u;   // the pose the palette and bounds hold
            float lo[3] = {0.0f, 0.0f, 0.0f};   // model-space skinned bounds (culling)
            float hi[3] = {0.0f, 0.0f, 0.0f};
            bool gpu = false;   // the vertex shader blends the palette
            int streamBinds = -1;   // the bind count the static bone stream was uploaded for
            std::string bound;      // what BindSkins last logged for it: it logs a change only
        };
        std::shared_ptr<Skin> skin;
        float       bakePivot[3] = {0.0f, 0.0f, 0.0f};
        // A batched prop has no buffer of its own; it lives in chunks_[chunk]
        // at palette slot `slot`.
        int         chunk = -1;
        int         slot = -1;
        // The grade its texels were baked at, when not the scene's.
        bool        ownGrade = false;
        float       gradeBrightness = 0.0f;
        float       gradeContrast = 0.0f;
    };

    // One chunk of batched props: its GPU buffers, its members by slot and the
    // palette the batched programs read.
    struct Chunk {
        PropChunkGpu gpu;
        std::vector<std::size_t> props;
        float mvp[kPropBatchSlots * 16] = {};   // per slot: viewProj x model, column-major
        float tint[kPropBatchSlots * 4] = {};
    };
    // Consecutive drawn props of one chunk showing one page: drawn as one, at
    // the place the first of them would have drawn. Draw order is kept
    // exactly: at a depth tie the first drawn wins, and reordering whole
    // chunks draws dark seams along the felt's edges.
    struct Run {
        int chunk = -1;
        int texture = -1;
        uint32_t mask = 0;
    };
    Run run_;
    void FlushRun();
    std::vector<Chunk> chunks_;
    Shader batched_;
    Shader batchedDyn_;
    Shader batchedFog_;
    const Shader* batchProgram_ = nullptr;   // this Draw's batched program
    bool batchingWanted_ = true;
    std::string batchingWhyOff_;
    bool batching_ = false;
    std::string batchingOff_;
    Shader skinned_;
    Shader skinnedDyn_;
    Shader skinnedFog_;
    const Shader* skinProgram_ = nullptr;   // this Draw's skinned program
    bool gpuSkinWanted_ = true;
    std::string gpuSkinWhyOff_;
    bool gpuSkin_ = false;
    std::string gpuSkinOff_;
    std::vector<float> skinSlots_ = std::vector<float>(static_cast<std::size_t>(kPropBatchSlots) * 16);
    void DrawSkinned(const Prop& p, const godot::GeometryInstance3D& node, float alpha);
    // An ADD prop (the blood splatter) is unshaded, so no animated light
    // reaches it, and it takes the grade's scale but not its offset, which is
    // already in what it adds onto: grade(dst + src) = s*dst + o + s*src.
    bool drawingAdditive_ = false;
    void ZeroOffsetIfAdditive();
    void BuildChunks(const assets::Room& room, const std::vector<int>& chunkOf, const std::vector<int>& slotOf,
                     const std::vector<int>& propOfRoom, std::size_t chunkCount);
    void UseBatchedProgram(const Shader& program, const Camera& camera);
    void SetSlot(Chunk& c, int slot, const float scale[3], const core::Mat4& model, float alpha);
    core::Mat4 viewProj_;   // this Draw's, for the palette's MVPs
    void UploadPalette(const Chunk& c);
    void JoinRun(std::size_t index);
    void DrawChunkMember(const Prop& p, const godot::GeometryInstance3D& node, float alpha);

    // Binds `texture`, or the 1x1 white fallback when it is -1. One place, so
    // a batch and a prop cannot disagree about what "untextured" means.
    void BindTexture(int texture);

    // Resolves each skinned prop's skeleton and its binds' bones.
    void BindSkins();
    // BindGpuSkin picks how a prop skins and returns it for the log. PoseSkin
    // brings the palette and the skinned bounds to the pose; SkinVertices
    // re-skins the CPU path's vertices, only for a prop that survived culling.
    std::string BindGpuSkin(Prop& p);
    void PoseSkin(Prop& p);
    void SkinVertices(Prop& p);
    void UseProgram(const Shader& program, const Camera& camera);
    void SetFogMixUniforms(const Shader& program) const;
    // Prop geometry with `node`'s global transform, the albedo change of the
    // prop's surface, and `alpha`.
    void DrawProp(const Prop& p, const godot::GeometryInstance3D& node, const core::Mat4& viewProj,
                  float alpha);
    // What DrawProp draws a prop with: its page (or a variant's), the scale
    // its colour takes (tint since load x probe ratio, before the
    // environment's), and its world transform.
    struct PropState {
        int texture = -1;
        bool variant = false;
        bool held = false;   // a held colour's variant: no brightening
        float scale[3] = {1.0f, 1.0f, 1.0f};
        core::Mat4 model;
    };
    PropState ResolveProp(const Prop& p, const godot::GeometryInstance3D& node) const;
    void CountProp(std::size_t index, const PropState& state);

    PropCensus census_;
    std::size_t propVertices_ = 0;
    std::size_t propColoured_ = 0;
    std::vector<core::Mat4> lastWorld_;     // per prop, sized at BindTree
    std::vector<uint8_t> pageSeen_;         // per texture, sized at Upload

    std::vector<MeshGpu> batches_;
    std::vector<Bounds>  bounds_;
    std::vector<int>     batchTexture_;
    std::vector<Prop>    props_;

    // The room's albedo, one GL texture per material. No atlas: a batch is one
    // material by construction, so this is one bind per draw that was already
    // going to happen, and the glTF's own UVs stay valid.
    std::vector<SpriteTexture> textures_;

    // Bound where a batch has no texture, so the shader path is identical and
    // there is no branch between textured and untextured geometry.
    SpriteTexture white_;
    int boundTexture_ = -2;   // -2 = nothing bound yet
    int textureBinds_ = 0;

    // The scene's current camera, for its cull_mask. Null draws every layer.
    const godot::Camera3D* camera3d_ = nullptr;

    // For the tree's PackedScene clones, and the template props each key draws.
    // A handful of hash lookups per frame (at most 8 shells x 2 meshes), no
    // allocation: the map is filled once at BindTree.
    const godot::SceneTree* tree_ = nullptr;
    uint64_t boundGeneration_ = 0;   // tree_->free_generation() the props' nodes match
    void RebindAfterFrees();
    std::unordered_map<std::string, std::vector<std::size_t>> templateProps_;

    // Partly transparent props this frame, drawn after the opaque ones.
    // Reserved to the prop count at BindTree so the frame never allocates.
    std::vector<std::size_t> blended_;
    bool cull_ = true;
    int  drawnBatches_ = 0;
    int  drawnTriangles_ = 0;
    int  hiddenProps_ = 0;
    Shader surface_;
    Shader surfaceDyn_;             // + the animated lights
    Shader surfaceFog_;             // + Environment fog (heaven, the menu)
    Shader surfaceMove_;            // + the moving light (the car ending)
    // Per Draw: whether the moving light burns near the camera, the prop
    // pass's program for everything outside its scope, and the camera.
    bool move_ = false;
    const Shader* propProgram_ = &surface_;
    const Camera* camera_ = nullptr;
    bool  moveOn_ = false;
    float movePos_[3] = {0.0f, 0.0f, 0.0f};
    float moveRange_ = 1.0f;
    float moveColour_[3] = {0.0f, 0.0f, 0.0f};
    bool  fogOn_ = false;
    bool  fogShade_ = false;
    float fogDensity_ = 0.0f;
    float fogColour_[3] = {0.0f, 0.0f, 0.0f};
    float fogClear_[3] = {0.0f, 0.0f, 0.0f};
    assets::FogMix fogMix_;
    const Shader* active_ = &surface_;
    float dyn_[4][3] = {};
    bool  dynLit_ = false;
    int   texturesUploaded_ = 0;
    bool  probeNoTextures_ = false;
    bool  probeBatchesOnly_ = false;
    float  envScale_ = 1.0f;
    std::vector<std::array<float, 3>> heldColours_;
    assets::ProbeGrid probes_;   // the room's; empty: props are never rescaled
    float  envOffset_ = 0.0f;
    bool   liveGrade_ = false;
    float  liveBrightness_ = 0.0f;
    float  liveContrast_ = 0.0f;
    float  activeOffset_ = 0.0f;   // the u_offset active_ holds: set only when it changes
    float  snapX_ = 0.0f;
    float  snapY_ = 0.0f;
    bool   affine_ = false;
};

}  // namespace render
