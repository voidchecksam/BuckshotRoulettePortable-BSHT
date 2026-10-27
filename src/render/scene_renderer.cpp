#include "render/scene_renderer.h"

#include "render/prop_batch_plan.h"
#include "render/prop_visibility.h"
#include "render/skin_gpu_plan.h"

#include "render/shaders/surface.h"

#include "assets/light_model.h"
#include "assets/moving_lights.h"
#include "assets/offstage.h"
#include "assets/skinning.h"

#include <cmath>
#include <cstring>

#include <cstdio>

namespace render {

namespace {

// A palette program that links but cannot find its palette would draw every
// prop it is handed with zero matrices: nothing on screen and "on" in the log.
// It counts as a failed compile, so the unbatched path draws instead.
bool HasPalette(const Shader& program) {
    return program.Uniform("u_mvp_cols") >= 0 && program.Uniform("u_tint") >= 0;
}

}  // namespace

bool SceneRenderer::BuildShaders() {
    if (!surface_.Compile(kSurfaceVertexSrc, kSurfaceFragmentSrc, "surface")) return false;
    // Optional: without it the animated lights are simply not drawn (Draw
    // falls back to the base program), never the whole room.
    if (!surfaceDyn_.Compile(kSurfaceDynVertexSrc, kSurfaceDynFragmentSrc, "surface dyn")) {
        std::printf("renderer: the animated-light program did not compile; flashes are not drawn\n");
    }
    if (!surfaceMove_.Compile(kSurfaceMoveVertexSrc, kSurfaceMoveFragmentSrc, "surface move")) {
        std::printf("renderer: the moving-light program did not compile; the street light is not drawn\n");
    }
    if (!surfaceFog_.Compile(kSurfaceFogVertexSrc, kSurfaceFogFragmentSrc, "surface fog")) {
        std::printf("renderer: the fog program did not compile; fog is not drawn\n");
    }
    // Prop batching: all three batched programs, or none. A chunk's props
    // have no buffers of their own to fall back to.
    batching_ = false;
    batchingOff_.clear();
    if (!batchingWanted_) {
        batchingOff_ = batchingWhyOff_.empty() ? "turned off" : batchingWhyOff_;
    } else if (!batched_.Compile(kBatchedVertexSrc, kBatchedFragmentSrc, "surface batched") ||
               !batchedDyn_.Compile(kBatchedDynVertexSrc, kBatchedDynFragmentSrc, "surface batched dyn") ||
               !batchedFog_.Compile(kBatchedFogVertexSrc, kBatchedFogFragmentSrc, "surface batched fog")) {
        batchingOff_ = "a batched program did not compile: " + Shader::LastError();
    } else if (!HasPalette(batched_) || !HasPalette(batchedDyn_) || !HasPalette(batchedFog_)) {
        batchingOff_ = "a batched program linked without u_mvp_cols / u_tint";
    } else {
        batching_ = true;
    }
    // GPU skinning: all three skinned programs, or the CPU skins every prop.
    gpuSkin_ = false;
    gpuSkinOff_.clear();
    if (!gpuSkinWanted_) {
        gpuSkinOff_ = gpuSkinWhyOff_.empty() ? "turned off" : gpuSkinWhyOff_;
    } else if (!skinned_.Compile(SkinnedVertexSource(kBatchedVertexSrc).c_str(), kBatchedFragmentSrc,
                                 "surface skinned") ||
               !skinnedDyn_.Compile(SkinnedVertexSource(kBatchedDynVertexSrc).c_str(), kBatchedDynFragmentSrc,
                                    "surface skinned dyn") ||
               !skinnedFog_.Compile(SkinnedVertexSource(kBatchedFogVertexSrc).c_str(), kBatchedFogFragmentSrc,
                                    "surface skinned fog")) {
        gpuSkinOff_ = "a skinned program did not compile: " + Shader::LastError();
    } else if (!HasPalette(skinned_) || !HasPalette(skinnedDyn_) || !HasPalette(skinnedFog_)) {
        gpuSkinOff_ = "a skinned program linked without u_mvp_cols / u_tint";
    } else {
        gpuSkin_ = true;
    }
    return true;
}

bool SceneRenderer::Upload(const assets::Room& room) {
    batches_.clear();
    bounds_.clear();
    batchTexture_.clear();
    props_.clear();
    textures_.clear();
    batches_.reserve(room.Batches().size());
    bounds_.reserve(room.Batches().size());
    batchTexture_.reserve(room.Batches().size());

    // The fallback comes first so it exists even if the room carries no pool.
    // Binding white means textured and untextured geometry take the same
    // shader path: no branch, no second program.
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        white_.Upload(white, 1, 1);
    }

    textures_.resize(room.Textures().Size());
    pageSeen_.assign(room.Textures().Size() + 1, 0);   // + the untextured (-1) page
    int texUploaded = 0;
    for (std::size_t i = 0; i < room.Textures().Size(); ++i) {
        const assets::BakedTexture& t = room.Textures().Textures()[i];
        const uint8_t* px = room.Textures().PixelsFor(static_cast<int>(i));
        // Surface pages: DXT1, DXT5 or RGBA8, filtered bilinearly. Anything
        // else is RGBA8 albedo (a room built without the surface bake).
        bool ok = false;
        if (px && (t.format == assets::TextureFormat::Dxt1 || t.format == assets::TextureFormat::Dxt5)) {
            ok = textures_[i].UploadSurfaceDxt(px, t.bytes, t.width, t.height,
                                               t.format == assets::TextureFormat::Dxt5);
        } else if (px && t.sourceGlb == "page") {
            ok = textures_[i].UploadSurfaceRgba8(px, t.width, t.height);
        } else if (px) {
            ok = textures_[i].Upload(px, t.width, t.height);
        }
        if (ok) {
            ++texUploaded;
        } else {
            std::printf("renderer: texture %lu (%s mat %d) failed to upload\n",
                        static_cast<unsigned long>(i), t.sourceGlb.c_str(),
                        t.materialIndex);
        }
    }
    texturesUploaded_ = texUploaded;
    if (room.Textures().Size() > 0) {
        std::printf("renderer: %d/%lu textures uploaded (%.2f MB)\n", texUploaded,
                    static_cast<unsigned long>(room.Textures().Size()),
                    static_cast<double>(room.Textures().ByteSize()) / 1048576.0);
    }

    int ownGraded = 0;
    int offstage = 0;
    int failed = 0;
    for (const assets::RoomBatch& b : room.Batches()) {
        MeshGpu gpu;
        if (gpu.Upload(b.mesh)) {
            batches_.push_back(std::move(gpu));

            Bounds bb{};
            for (int a = 0; a < 3; ++a) {
                // A batch with no bounds must never be culled: treat it as
                // infinite rather than as empty, or it silently disappears.
                bb.lo[a] = b.HasBounds() ? b.boundsMin[a] : -1e30f;
                bb.hi[a] = b.HasBounds() ? b.boundsMax[a] :  1e30f;
            }
            bounds_.push_back(bb);
            batchTexture_.push_back(b.texture);
        } else {
            // Report rather than skip quietly: a batch that fails to upload is
            // a chunk of missing room, and finding it later from the visual
            // alone is very hard.
            std::printf("renderer: batch from %s failed to upload (%d verts)\n",
                        b.sourceGlb.c_str(), b.mesh.VertexCount());
            ++failed;
        }
    }

    probes_ = room.Probes();
    // Prop batching: which props share a chunk (render/prop_batch_plan.h).
    // Skinned props, PackedScene templates and the moving light's scope keep
    // their own buffers.
    const std::size_t roomProps = room.Props().size();
    std::vector<int> chunkOf(roomProps, -1);
    std::vector<int> slotOf(roomProps, -1);
    std::vector<int> propOfRoom(roomProps, -1);
    std::size_t chunkCount = 0;
    chunks_.clear();
    if (batching_) {
        std::vector<BatchProp> plan(roomProps);
        for (std::size_t i = 0; i < roomProps; ++i) {
            const assets::RoomProp& rp = room.Props()[i];
            plan[i] = BatchProp{rp.texture, static_cast<uint32_t>(rp.mesh.VertexCount()), PropCanBatch(rp)};
        }
        const std::vector<PropChunk> chunks = render::BuildChunks(plan, kPropBatchSlots, 65535u);
        chunkCount = chunks.size();
        for (std::size_t c = 0; c < chunks.size(); ++c) {
            for (std::size_t slot = 0; slot < chunks[c].props.size(); ++slot) {
                chunkOf[static_cast<std::size_t>(chunks[c].props[slot])] = static_cast<int>(c);
                slotOf[static_cast<std::size_t>(chunks[c].props[slot])] = static_cast<int>(slot);
            }
        }
    }
    for (std::size_t ri = 0; ri < roomProps; ++ri) {
        const assets::RoomProp& rp = room.Props()[ri];
        Prop p;
        const bool skinned = !rp.mesh.joints.empty() && rp.mesh.weights.size() == rp.mesh.joints.size();
        p.chunk = chunkOf[ri];
        p.slot = slotOf[ri];
        if (p.chunk < 0 && !p.gpu.Upload(rp.mesh, skinned)) {
            std::printf("renderer: prop %s failed to upload (%d verts)\n",
                        rp.nodePath.c_str(), rp.mesh.VertexCount());
            ++failed;
            continue;
        }
        p.nodePath = rp.nodePath;
        p.texture = rp.texture;
        p.surface = rp.surface;
        p.variants = rp.variants;
        p.templateKey = rp.templateKey;
        p.hasBakePivot = rp.hasBakePivot;
        std::copy_n(rp.bakePivot, 3, p.bakePivot);
        p.movingLightScope = assets::InMovingLightScope(rp.nodePath);
        p.offstage = rp.hasBakePivot && assets::InOffstageScope(rp.nodePath);
        if (p.offstage) {
            std::printf("renderer: offstage prop %s (dark beyond the probes)\n", rp.nodePath.c_str());
            ++offstage;
        }
        p.ownGrade = rp.HasOwnGrade();
        p.gradeBrightness = rp.bakeBrightness;
        p.gradeContrast = rp.bakeContrast;
        ownGraded += p.ownGrade ? 1 : 0;
        if (skinned) {
            p.skin = std::make_shared<Prop::Skin>();
            p.skin->bind.positions = rp.mesh.positions;
            p.skin->bind.joints = rp.mesh.joints;
            p.skin->bind.weights = rp.mesh.weights;
        }
        // Model-space bounds, from the prop's own vertices: culling moves them
        // with the node each frame. The room's boundsMin/Max are the world box
        // at the rest pose, which an animated prop leaves behind (the item
        // compartments would be culled while on screen). No vertices, no
        // culling.
        const std::vector<float>& v = rp.mesh.positions;
        for (int a = 0; a < 3; ++a) {
            p.bounds.lo[a] = v.size() >= 3 ? 1e30f : -1e30f;
            p.bounds.hi[a] = v.size() >= 3 ? -1e30f : 1e30f;
        }
        for (std::size_t i = 0; i + 2 < v.size(); i += 3) {
            for (int a = 0; a < 3; ++a) {
                const float x = v[i + static_cast<std::size_t>(a)];
                if (x < p.bounds.lo[a]) p.bounds.lo[a] = x;
                if (x > p.bounds.hi[a]) p.bounds.hi[a] = x;
            }
        }
        p.hasModelBounds = v.size() >= 3;
        propVertices_ += static_cast<std::size_t>(rp.mesh.VertexCount());
        if (rp.mesh.HasColours()) propColoured_ += static_cast<std::size_t>(rp.mesh.VertexCount());
        propOfRoom[ri] = static_cast<int>(props_.size());
        props_.push_back(std::move(p));
    }
    if (batching_) BuildChunks(room, chunkOf, slotOf, propOfRoom, chunkCount);
    std::printf("renderer: %d offstage props\n", offstage);
    if (ownGraded > 0) {
        std::printf("renderer: %d props baked at their own grade (graded against it at runtime)\n", ownGraded);
    }

    if (failed) std::printf("renderer: %d uploads failed\n", failed);

    return !batches_.empty() || !props_.empty();
}

int SceneRenderer::BindTree(const godot::SceneTree& tree) {
    camera3d_ = tree.get_current_camera();
    tree_ = &tree;
    boundGeneration_ = tree.free_generation();
    templateProps_.clear();
    blended_.reserve(props_.size());
    lastWorld_.assign(props_.size(), core::Mat4{});
    int unresolved = 0;
    for (std::size_t i = 0; i < props_.size(); ++i) {
        Prop& p = props_[i];
        // A template prop has no node of its own: it draws for every clone
        // carrying its key.
        if (!p.templateKey.empty()) {
            templateProps_[p.templateKey].push_back(i);
            continue;
        }
        // Resolved once. A per-frame path lookup for a mapping that never
        // changes is exactly the string work the frame loop was cleared of.
        p.node = tree.get_node_as<godot::GeometryInstance3D>(p.nodePath);
        if (!p.node) {
            std::printf("renderer: prop %s has no node in the runtime scene\n",
                        p.nodePath.c_str());
            ++unresolved;
        }
    }
    BindSkins();
    return unresolved;
}

void SceneRenderer::BindSkins() {
    for (Prop& p : props_) {
        if (!p.skin) continue;
        Prop::Skin& s = *p.skin;
        s.skeleton = nullptr;
        s.bindBone.clear();
        s.bindPose.clear();
        s.palette.clear();
        s.gpu = false;
        s.version = ~0u;
        s.paletteVersion = ~0u;
        std::copy_n(p.bounds.lo, 3, s.lo);   // until a pose: the bind box
        std::copy_n(p.bounds.hi, 3, s.hi);
        std::string how = "no skeleton: drawn at its bind pose";
        const auto* mi = godot::cast_to<godot::MeshInstance3D>(p.node);
        if (mi && mi->has_skin() && tree_) {
            s.skeleton = godot::cast_to<godot::Skeleton3D>(tree_->get_node_relative(mi, mi->get_skeleton_path()));
        }
        if (s.skeleton) {
            for (const godot::MeshInstance3D::SkinBind& b : mi->get_skin_binds()) {
                s.bindBone.push_back(s.skeleton->find_bone(b.name));
                core::Mat4 m;
                std::copy_n(b.pose, 16, m.m);
                s.bindPose.push_back(m);
            }
            s.palette.resize(s.bindPose.size());
            how = BindGpuSkin(p);
        }
        // Rebinds follow every node free (RebindAfterFrees): log a change only.
        if (how != s.bound) {
            s.bound = how;
            std::printf("renderer: skin %s: %lu verts, %lu binds, %s\n", p.nodePath.c_str(),
                        static_cast<unsigned long>(s.bind.positions.size() / 3),
                        static_cast<unsigned long>(s.bindPose.size()), how.c_str());
        }
    }
}

// The GPU blends the palette when the programs compiled and the binds fit it;
// otherwise the CPU re-skins each time the pose moves. The bone stream is
// static: uploaded once per bind count, kept across rebinds. Returns how the
// prop skins, for the log.
std::string SceneRenderer::BindGpuSkin(Prop& p) {
    Prop::Skin& s = *p.skin;
    const int binds = static_cast<int>(s.bindPose.size());
    if (!gpuSkin_) return "CPU (GPU skinning off)";
    if (p.movingLightScope) return "CPU (in the moving light's scope: the MOVE program)";
    if (binds > kMaxGpuSkinBinds) return "CPU (more binds than the palette holds)";
    if (s.streamBinds != binds) {
        std::vector<SkinVertex> stream;
        if (!BuildSkinStream(s.bind, binds, stream) || !p.gpu.UploadSkinStream(stream)) {
            return "CPU (no bone stream)";
        }
        s.streamBinds = binds;
    }
    s.gpu = true;
    return "GPU";
}

// palette[j] = bone_global_pose * bind_pose (skeleton_3d.cpp:296), in the
// MeshInstance3D's space; u_mvp applies the node's transform.
void SceneRenderer::PoseSkin(Prop& p) {
    Prop::Skin& s = *p.skin;
    if (!s.skeleton || s.skeleton->get_pose_version() == s.paletteVersion) return;
    core::ProfScope prof(profiler_, core::ProfSection::Skinning);
    s.paletteVersion = s.skeleton->get_pose_version();
    for (std::size_t j = 0; j < s.bindPose.size(); ++j) {
        const int bone = s.bindBone[j];
        s.palette[j] = bone >= 0 ? core::Mul(s.skeleton->get_bone_global_pose(bone), s.bindPose[j]) : s.bindPose[j];
    }
    SkinnedBounds(p.bounds.lo, p.bounds.hi, s.palette, s.lo, s.hi);
}

// The GPU path needs nothing here: DrawSkinned hands the palette to the shader.
void SceneRenderer::SkinVertices(Prop& p) {
    Prop::Skin& s = *p.skin;
    if (s.gpu || !s.skeleton || s.version == s.paletteVersion) return;
    core::ProfScope prof(profiler_, core::ProfSection::Skinning);
    s.version = s.paletteVersion;
    assets::SkinPositions(s.bind, s.palette, s.skinned);
    p.gpu.UpdatePositions(s.skinned.data(), s.skinned.size() / 3);
}

// A script freed nodes (the signed waiver's queue_free): every cached pointer
// is re-resolved by path, so a freed prop finds no node and is not drawn.
// Paths only: a prop that never had a node is not looked up or logged again.
void SceneRenderer::RebindAfterFrees() {
    if (!tree_ || tree_->free_generation() == boundGeneration_) return;
    boundGeneration_ = tree_->free_generation();
    camera3d_ = tree_->get_current_camera();
    for (Prop& p : props_) {
        if (p.node) p.node = tree_->get_node_as<godot::GeometryInstance3D>(p.nodePath);
    }
    BindSkins();
}

int SceneRenderer::TriangleCount() const {
    int n = 0;
    for (const MeshGpu& b : batches_) n += b.TriangleCount();
    for (const Prop& p : props_) {
        n += p.chunk >= 0 ? chunks_[static_cast<std::size_t>(p.chunk)].gpu.MemberTriangles(p.slot)
                          : p.gpu.TriangleCount();
    }
    return n;
}

// Uploads each planned chunk: its members' vertices tagged with their slot,
// their indices rebased into the chunk's buffer.
void SceneRenderer::BuildChunks(const assets::Room& room, const std::vector<int>& chunkOf,
                                const std::vector<int>& slotOf, const std::vector<int>& propOfRoom,
                                std::size_t chunkCount) {
    struct Build {
        std::vector<Vertex> vertices;
        std::vector<uint16_t> indices;
        std::vector<IndexRange> ranges;
        std::vector<std::size_t> props;
    };
    std::vector<Build> builds(chunkCount);
    for (std::size_t ri = 0; ri < chunkOf.size(); ++ri) {   // room order = slot order within a chunk
        if (chunkOf[ri] < 0) continue;
        Build& b = builds[static_cast<std::size_t>(chunkOf[ri])];
        const assets::Mesh& mesh = room.Props()[ri].mesh;
        const auto base = static_cast<uint16_t>(b.vertices.size());
        AppendVertices(mesh, b.vertices, static_cast<float>(slotOf[ri]));
        const auto first = static_cast<uint32_t>(b.indices.size());
        for (uint16_t idx : mesh.indices) b.indices.push_back(static_cast<uint16_t>(idx + base));
        b.ranges.push_back(IndexRange{first, static_cast<uint32_t>(mesh.indices.size())});
        b.props.push_back(static_cast<std::size_t>(propOfRoom[ri]));
    }
    chunks_.resize(chunkCount);
    for (std::size_t c = 0; c < chunkCount; ++c) {
        Chunk& chunk = chunks_[c];
        chunk.props = std::move(builds[c].props);
        if (!chunk.gpu.Upload(builds[c].vertices, std::move(builds[c].indices), std::move(builds[c].ranges))) {
            std::printf("renderer: prop chunk %lu failed to upload (%lu props)\n", static_cast<unsigned long>(c),
                        static_cast<unsigned long>(chunk.props.size()));
        }
    }
}

// The batched program for this Draw, with what every chunk draw shares.
void SceneRenderer::UseBatchedProgram(const Shader& program, const Camera& camera) {
    active_ = &program;
    program.Use();
    program.SetVec2("u_snap", snapX_, snapY_);
    program.SetFloat("u_affine", affine_ ? 1.0f : 0.0f);
    program.SetInt("u_tex", 0);
    program.SetVec3("u_offset", envOffset_, envOffset_, envOffset_);
    activeOffset_ = envOffset_;
    if (&program == &batchedDyn_ || &program == &skinnedDyn_) {
        program.SetVec3("u_dyn0", dyn_[0][0], dyn_[0][1], dyn_[0][2]);
        program.SetVec3("u_dyn1", dyn_[1][0], dyn_[1][1], dyn_[1][2]);
        program.SetVec3("u_dyn2", dyn_[2][0], dyn_[2][1], dyn_[2][2]);
        program.SetVec3("u_dyn3", dyn_[3][0], dyn_[3][1], dyn_[3][2]);
    }
    if (&program == &batchedFog_ || &program == &skinnedFog_) {
        program.SetVec3("u_fog", fogColour_[0], fogColour_[1], fogColour_[2]);
        program.SetFloat("u_fog_density", fogDensity_);
        SetFogMixUniforms(program);
        program.SetVec2("u_proj_scale", camera.Proj().m[0], camera.Proj().m[5]);
    }
}

// A prop's palette entry: its MVP (exactly the u_mvp DrawProp would set) and
// its colour scale times the environment's, with alpha.
void SceneRenderer::SetSlot(Chunk& c, int slot, const float scale[3], const core::Mat4& model, float alpha) {
    const core::Mat4 mvp = core::Mul(viewProj_, model);
    std::copy_n(mvp.m, 16, c.mvp + static_cast<std::size_t>(slot) * 16);
    float* t = c.tint + static_cast<std::size_t>(slot) * 4;
    t[0] = scale[0] * envScale_;
    t[1] = scale[1] * envScale_;
    t[2] = scale[2] * envScale_;
    t[3] = alpha;
}

void SceneRenderer::UploadPalette(const Chunk& c) {
    const GLint mvp = active_->Uniform("u_mvp_cols");
    const GLint tint = active_->Uniform("u_tint");
    if (mvp >= 0) glUniform4fv(mvp, kPropBatchSlots * 4, c.mvp);
    if (tint >= 0) glUniform4fv(tint, kPropBatchSlots, c.tint);
}

// An opaque, base-pass batched prop joins the run being built, or starts one.
void SceneRenderer::JoinRun(std::size_t index) {
    const Prop& p = props_[index];
    Chunk& c = chunks_[static_cast<std::size_t>(p.chunk)];
    const PropState st = ResolveProp(p, *p.node);
    CountProp(index, st);
    SetSlot(c, p.slot, st.scale, st.model, 1.0f);
    if (run_.chunk != p.chunk || run_.texture != st.texture) {
        FlushRun();
        run_.chunk = p.chunk;
        run_.texture = st.texture;
    }
    run_.mask |= 1u << static_cast<uint32_t>(p.slot);
    ++census_.batched;
    drawnTriangles_ += c.gpu.MemberTriangles(p.slot);
}

// Draws the run being built, if any: its chunk's palette, its page, one draw.
void SceneRenderer::FlushRun() {
    if (run_.chunk < 0 || run_.mask == 0) {
        run_ = Run{};
        return;
    }
    Chunk& c = chunks_[static_cast<std::size_t>(run_.chunk)];
    if (active_ != batchProgram_) UseBatchedProgram(*batchProgram_, *camera_);
    UploadPalette(c);
    BindTexture(run_.texture);
    c.gpu.DrawJoined(run_.texture, run_.mask);
    census_.listRebuilds += c.gpu.TakeRebuilds();
    ++drawnBatches_;
    ++census_.batchedDraws;
    run_ = Run{};
}

// A batched prop that draws alone (blended, or forced by the overlay): its
// slot set, the palette uploaded, its own index range.
void SceneRenderer::DrawChunkMember(const Prop& p, const godot::GeometryInstance3D& node, float alpha) {
    Chunk& c = chunks_[static_cast<std::size_t>(p.chunk)];
    const Shader* program = drawingAdditive_ && batchProgram_ == &batchedDyn_ ? &batched_ : batchProgram_;
    if (active_ != program) UseBatchedProgram(*program, *camera_);
    ZeroOffsetIfAdditive();
    const PropState st = ResolveProp(p, node);
    if (alpha >= 1.0f && &node == p.node) CountProp(static_cast<std::size_t>(&p - props_.data()), st);
    SetSlot(c, p.slot, st.scale, st.model, alpha);
    UploadPalette(c);
    BindTexture(st.texture);
    c.gpu.DrawMember(p.slot);
    ++drawnBatches_;
    drawnTriangles_ += c.gpu.MemberTriangles(p.slot);
}

// A GPU-skinned prop: its binds' MVPs (and the rest pose's) in the palette,
// its colour scale in slot 0's tint, the bone stream beside its vertices.
void SceneRenderer::DrawSkinned(const Prop& p, const godot::GeometryInstance3D& node, float alpha) {
    const Shader* program = drawingAdditive_ && skinProgram_ == &skinnedDyn_ ? &skinned_ : skinProgram_;
    if (active_ != program) UseBatchedProgram(*program, *camera_);
    ZeroOffsetIfAdditive();
    const PropState st = ResolveProp(p, node);
    if (alpha >= 1.0f && &node == p.node) CountProp(static_cast<std::size_t>(&p - props_.data()), st);
    const Prop::Skin& s = *p.skin;
    SkinClipPalette(viewProj_, st.model, s.palette, skinSlots_.data());
    const float tint[4] = {st.scale[0] * envScale_, st.scale[1] * envScale_, st.scale[2] * envScale_, alpha};
    const GLint mvp = active_->Uniform("u_mvp_cols");
    const GLint tintAt = active_->Uniform("u_tint");
    if (mvp >= 0) glUniform4fv(mvp, static_cast<GLsizei>((s.palette.size() + 1) * 4), skinSlots_.data());
    if (tintAt >= 0) glUniform4fv(tintAt, 1, tint);
    BindTexture(st.texture);
    p.gpu.DrawSkinned();
    ++drawnBatches_;
    drawnTriangles_ += p.gpu.TriangleCount();
}

void SceneRenderer::ZeroOffsetIfAdditive() {
    if (drawingAdditive_ && active_) {
        active_->SetVec3("u_offset", 0.0f, 0.0f, 0.0f);
        activeOffset_ = 0.0f;
    }
}

bool SceneRenderer::OverridePropVisible(const std::string& nodePath, bool visible) {
    bool found = false;
    for (Prop& p : props_) {
        if (p.nodePath == nodePath) {
            p.forceVisible = visible ? 1 : 0;
            found = true;
        }
    }
    return found;
}

void SceneRenderer::BindTexture(int texture) {
    // Redundant binds are skipped. Batches are merged per material, so runs of
    // the same texture are common and a bind is not free on this hardware.
    if (texture == boundTexture_) return;
    boundTexture_ = texture;
    ++textureBinds_;

    glActiveTexture(GL_TEXTURE0);
    if (!probeNoTextures_ && texture >= 0 && texture < static_cast<int>(textures_.size()) &&
        textures_[static_cast<std::size_t>(texture)].Valid()) {
        glBindTexture(GL_TEXTURE_2D,
                      textures_[static_cast<std::size_t>(texture)].Id());
        return;
    }
    glBindTexture(GL_TEXTURE_2D, white_.Valid() ? white_.Id() : 0);
}

// Binds `program` with every uniform a pass needs, world-space MVP, identity
// model, the environment's grade, opaque. One place, so a pass switch cannot
// leave a uniform from the other program behind.
// The FOG programs' linear-light mix (assets::FogWeight): the grade's grey
// curve, the fog's luminance and its own grey.
void SceneRenderer::SetFogMixUniforms(const Shader& program) const {
    const assets::FogCurve& c = fogMix_.curve;
    program.SetVec4("u_fog_curve", c.whiteInv, c.brightness, c.contrast, fogMix_.luminance);
    program.SetFloat("u_fog_grey", fogMix_.grey);
}

void SceneRenderer::UseProgram(const Shader& program, const Camera& camera) {
    active_ = &program;
    const Shader& sh = program;
    sh.Use();
    if (&program == &surfaceFog_) {
        sh.SetVec3("u_fog", fogColour_[0], fogColour_[1], fogColour_[2]);
        sh.SetFloat("u_fog_density", fogDensity_);
        SetFogMixUniforms(sh);
        const float* eye = camera.Eye();
        sh.SetVec3("u_eye", eye[0], eye[1], eye[2]);
        sh.SetMat4("u_model", core::Mat4{});
    }
    if (&program == &surfaceMove_) {
        sh.SetVec3("u_light_pos", movePos_[0], movePos_[1], movePos_[2]);
        sh.SetFloat("u_light_range", moveRange_);
        // Godot grades the street light with the live Environment; its gain
        // was fitted at the rest grade, so the term takes the live slope over
        // the rest's: x6.2 at the ending, 1 at the table.
        sh.SetVec3("u_light", moveColour_[0] * envScale_, moveColour_[1] * envScale_, moveColour_[2] * envScale_);
        sh.SetMat4("u_model", core::Mat4{});
    }
    if (&program == &surfaceDyn_) {
        sh.SetVec3("u_dyn0", dyn_[0][0], dyn_[0][1], dyn_[0][2]);
        sh.SetVec3("u_dyn1", dyn_[1][0], dyn_[1][1], dyn_[1][2]);
        sh.SetVec3("u_dyn2", dyn_[2][0], dyn_[2][1], dyn_[2][2]);
        sh.SetVec3("u_dyn3", dyn_[3][0], dyn_[3][1], dyn_[3][2]);
    }
    sh.SetVec2("u_snap", snapX_, snapY_);
    sh.SetFloat("u_affine", affine_ ? 1.0f : 0.0f);
    sh.SetInt("u_tex", 0);
    sh.SetVec3("u_scale", envScale_, envScale_, envScale_);
    sh.SetVec3("u_offset", envOffset_, envOffset_, envOffset_);
    activeOffset_ = envOffset_;
    sh.SetFloat("u_alpha", 1.0f);
    sh.SetMat4("u_mvp", camera.ViewProj());
}

void SceneRenderer::Draw(const Camera& camera) {
    // The DYN program only while an animated light burns: the base one is the
    // cost the hardware probe measured holding 60. The FOG program in a fogged
    // scene (no fogged scene has an animated light): for props always, for
    // merged batches only when the bake did not fog them (assets/scene_fog.h).
    // The MOVE program lights only the moving light's scope (the car), prop by
    // prop: it has no shadows (assets/moving_lights.h).
    const bool fogReady = fogOn_ && surfaceFog_.Valid();
    move_ = !fogReady && moveOn_ && surfaceMove_.Valid();
    const Shader& lit = dynLit_ && surfaceDyn_.Valid() ? surfaceDyn_ : surface_;
    const Shader& batchProgram = fogReady && fogShade_ ? surfaceFog_ : lit;
    const Shader& propProgram = fogReady ? surfaceFog_ : lit;
    propProgram_ = &propProgram;
    batchProgram_ = fogReady ? &batchedFog_ : (dynLit_ && surfaceDyn_.Valid() ? &batchedDyn_ : &batched_);
    skinProgram_ = fogReady ? &skinnedFog_ : (dynLit_ && surfaceDyn_.Valid() ? &skinnedDyn_ : &skinned_);
    camera_ = &camera;
    if (!batchProgram.Valid() || !propProgram.Valid()) return;

    if (fogOn_) {
        // The background is the fog (fog_sky_affect over the clear colour).
        glClearColor(fogClear_[0], fogClear_[1], fogClear_[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    UseProgram(batchProgram, camera);
    boundTexture_ = -2;
    textureBinds_ = 0;

    drawnBatches_ = 0;
    drawnTriangles_ = 0;

    core::Frustum frustum;
    if (cull_) frustum.FromViewProj(camera.ViewProj());

    // Merged static geometry: already world-space, so one shared MVP.
    const core::Mat4 viewProj = camera.ViewProj();
    viewProj_ = viewProj;

    for (std::size_t i = 0; i < batches_.size(); ++i) {
        if (cull_ && i < bounds_.size() &&
            !frustum.IntersectsAabb(bounds_[i].lo, bounds_[i].hi)) {
            continue;
        }
        BindTexture(i < batchTexture_.size() ? batchTexture_[i] : -1);
        batches_[i].Draw();
        ++drawnBatches_;
        drawnTriangles_ += batches_[i].TriangleCount();
    }

    if (&propProgram != active_) UseProgram(propProgram, camera);

    RebindAfterFrees();
    hiddenProps_ = 0;
    const uint32_t cullMask = camera3d_ ? camera3d_->get_cull_mask() : 0xFFFFFu;

    // Props: model space, so each needs its node's global transform. State is
    // READ from the godot:: tree; nothing pushes visibility into the renderer.
    blended_.clear();
    census_ = PropCensus{};
    std::fill(pageSeen_.begin(), pageSeen_.end(), uint8_t{0});
    const std::size_t propCount = probeBatchesOnly_ ? 0 : props_.size();   // probe: draw-call cost
    for (std::size_t i = 0; i < propCount; ++i) {
        Prop& p = props_[i];
        if (!p.templateKey.empty()) continue;   // drawn below, per clone
        bool visible = p.node != nullptr && PropIsDrawn(*p.node, cullMask);
        // The debug overlay can force a prop either way, so the device can
        // answer "is it hidden or is it missing?" without another build.
        const bool forced = p.forceVisible >= 0;
        if (forced) visible = p.forceVisible != 0 && p.node != nullptr;
        if (!visible) { ++hiddenProps_; continue; }

        // Cull where the node is NOW, not where it was baked. A skinned prop
        // culls by its skinned bounds (its bones carry it outside its
        // bind-pose box), and only a prop that survives is re-skinned.
        if (p.skin) PoseSkin(p);
        if (cull_ && p.hasModelBounds) {
            float lo[3];
            float hi[3];
            core::TransformAabb(p.node->get_global_transform(), p.skin ? p.skin->lo : p.bounds.lo,
                                p.skin ? p.skin->hi : p.bounds.hi, lo, hi);
            if (!frustum.IntersectsAabb(lo, hi)) continue;
        }
        if (p.skin) SkinVertices(p);

        // A forced prop draws opaque: the point is to see it.
        if (!forced && PropIsBlended(*p.node)) {
            blended_.push_back(i);
            continue;
        }
        if (p.chunk >= 0 && !forced) {
            JoinRun(i);   // drawn when its run ends
            continue;
        }
        FlushRun();
        DrawProp(p, *p.node, viewProj, 1.0f);
    }
    FlushRun();

    // PackedScene clones (the shells ShellSpawner instantiates), each drawn
    // with the template props baked for its mesh, by the same draw rule.
    // PORT: clones draw in the opaque pass; no instanced scene fades.
    if (tree_ && !probeBatchesOnly_) {
        for (const godot::MeshInstance3D* clone : tree_->template_instances()) {
            if (!PropIsDrawn(*clone, cullMask)) continue;
            ++census_.clones;
            const auto it = templateProps_.find(clone->get_template_key());
            if (it == templateProps_.end()) continue;
            for (std::size_t i : it->second) DrawProp(props_[i], *clone, viewProj, PropAlpha(*clone));
        }
    }

    // Partly transparent props last, over the opaque room, without writing
    // depth, as the sprite batch and Label3D pass do.
    // PORT: not sorted back to front. The blended set is a handful of flashes
    // and fades (the muzzle-flash cone), rarely more than one on screen.
    if (!blended_.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        census_.blended = static_cast<int>(blended_.size());
        for (std::size_t i : blended_) {
            // Godot's BLEND_MODE_ADD is src * alpha + dst.
            const bool additive = PropIsAdditive(*props_[i].node);
            glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            drawingAdditive_ = additive;
            glDepthMask(PropWritesDepth(*props_[i].node) ? GL_TRUE : GL_FALSE);
            DrawProp(props_[i], *props_[i].node, viewProj, PropAlpha(*props_[i].node));
            if (additive) {
                drawingAdditive_ = false;
                active_ = nullptr;   // the next draw re-Uses its program: its offset comes back
            }
        }
        if (!active_) UseProgram(*propProgram_, *camera_);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   // not an additive prop's
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // Leave the uniforms holding the shared matrix and no prop's colour so a
    // later caller that forgets to set them draws world-space geometry
    // correctly rather than in the last prop's frame and colour.
    const Shader& sh = *active_;
    sh.SetVec3("u_scale", envScale_, envScale_, envScale_);
    sh.SetVec3("u_offset", envOffset_, envOffset_, envOffset_);   // not an own-graded prop's
    activeOffset_ = envOffset_;
    sh.SetFloat("u_alpha", 1.0f);
    sh.SetMat4("u_mvp", viewProj);
    if (active_ == &surfaceFog_ || active_ == &surfaceMove_) sh.SetMat4("u_model", core::Mat4{});
}

SceneRenderer::PropState SceneRenderer::ResolveProp(const Prop& p,
                                                    const godot::GeometryInstance3D& node) const {
    PropState st;
    // A material a script swapped in (a shell live or blank, a blinker on or
    // off) has its own baked page: Godot's colour for that material, exactly.
    // Otherwise the page holds the colour at load and a script's albedo change
    // since then scales it in display space, which is exact for the
    // white-to-colour fades the scripts do.
    st.texture = p.texture;
    godot::Color tint = node.get_surface_tint_since_load(p.surface);
    if (!p.variants.empty()) {
        const godot::Color a = node.get_active_surface_albedo(p.surface);
        const godot::Color e = node.get_active_surface_emission(p.surface);
        const float albedo[4] = {a.r, a.g, a.b, a.a};
        const float emission[3] = {e.r, e.g, e.b};
        const int variant = assets::FindVariantTexture(p.variants, albedo, emission,
                                                       assets::TextureKeyHash(node.get_active_surface_texture(p.surface)));
        if (variant >= 0) {
            st.texture = variant;
            st.variant = true;
            for (const auto& c : heldColours_) st.held = st.held || (c[0] == a.r && c[1] == a.g && c[2] == a.b);
            tint = {1.0f, 1.0f, 1.0f, 1.0f};
        }
    }
    st.model = node.get_global_transform();
    // A moved prop carries its bake-pose light scaled by how much brighter the
    // static light is where it is now: 1 at its bake pose.
    float ratio[3] = {1.0f, 1.0f, 1.0f};
    if (p.hasBakePivot) {
        const float now[3] = {st.model.m[12], st.model.m[13], st.model.m[14]};
        if (p.offstage) {
            assets::OffstageRatio(probes_, now, p.bakePivot, ratio);
        } else {
            assets::PropRatio(probes_, now, p.bakePivot, ratio);
        }
    }
    st.scale[0] = tint.r * ratio[0];
    st.scale[1] = tint.g * ratio[1];
    st.scale[2] = tint.b * ratio[2];
    return st;
}

// One drawn, opaque, base-pass prop into the census (see PropCensus).
void SceneRenderer::CountProp(std::size_t index, const PropState& st) {
    const Prop& p = props_[index];
    ++census_.drawn;
    const bool still = index < lastWorld_.size() &&
                       std::memcmp(lastWorld_[index].m, st.model.m, sizeof st.model.m) == 0;
    if (index < lastWorld_.size()) lastWorld_[index] = st.model;
    if (still) ++census_.still;
    const bool scaled = st.scale[0] != 1.0f || st.scale[1] != 1.0f || st.scale[2] != 1.0f;
    if (st.variant) ++census_.variants;
    if (scaled) ++census_.scaled;
    if (p.skin) ++census_.skinned;
    if (p.movingLightScope) ++census_.moveScope;
    // Keyed by the page it draws with now: variants that share a page merge too.
    if (!still || scaled || p.skin || p.movingLightScope) return;
    ++census_.mergeable;
    const auto t = static_cast<std::size_t>(st.texture + 1);   // -1 (untextured) is a page too
    if (t < pageSeen_.size() && !pageSeen_[t]) {
        pageSeen_[t] = 1;
        ++census_.mergeablePages;
    }
}

void SceneRenderer::DrawProp(const Prop& p, const godot::GeometryInstance3D& node,
                             const core::Mat4& viewProj, float alpha) {
    if (p.chunk >= 0) {
        DrawChunkMember(p, node, alpha);
        return;
    }
    if (p.skin && p.skin->gpu) {
        DrawSkinned(p, node, alpha);
        return;
    }
    const Shader& lit = drawingAdditive_ && propProgram_ == &surfaceDyn_ ? surface_ : *propProgram_;
    const Shader& want = move_ && p.movingLightScope ? surfaceMove_ : lit;
    if (&want != active_) UseProgram(want, *camera_);
    const Shader& sh = *active_;
    // A prop baked at its own grade is graded against it: the car, baked at
    // the ending's grade, draws its texels as baked when that grade is live.
    float gradeScale = envScale_, gradeOffset = envOffset_;
    if (p.ownGrade && liveGrade_) {
        assets::light::PosterizedEnvironmentUniforms(liveBrightness_, liveContrast_, &gradeScale, &gradeOffset,
                                                     p.gradeBrightness, p.gradeContrast);
    }
    const PropState st = ResolveProp(p, node);
    if (st.held && gradeScale > 1.0f) {
        gradeScale = 1.0f;
        gradeOffset = 0.0f;
    }
    const float offset = drawingAdditive_ ? 0.0f : gradeOffset;   // an ADD prop takes none
    if (offset != activeOffset_) {
        sh.SetVec3("u_offset", offset, offset, offset);
        activeOffset_ = offset;
    }
    if (alpha >= 1.0f && &node == p.node) CountProp(static_cast<std::size_t>(&p - props_.data()), st);
    sh.SetVec3("u_scale", st.scale[0] * gradeScale, st.scale[1] * gradeScale, st.scale[2] * gradeScale);
    sh.SetFloat("u_alpha", alpha);
    sh.SetMat4("u_mvp", core::Mul(viewProj, st.model));
    if (active_ == &surfaceFog_ || active_ == &surfaceMove_) sh.SetMat4("u_model", st.model);
    BindTexture(st.texture);
    p.gpu.Draw();
    ++drawnBatches_;
    drawnTriangles_ += p.gpu.TriangleCount();
}

}  // namespace render
