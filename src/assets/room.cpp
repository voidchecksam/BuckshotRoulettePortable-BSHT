#include "assets/room.h"

#include <cmath>

#include "core/mat4.h"

#include <cstdio>
#include <unordered_map>

namespace assets {

namespace {

// Appends `src`, already transformed to world space, onto `dst`, growing the
// batch's world-space bounds as it goes.
void AppendMesh(RoomBatch& batch, const Mesh& src, int32_t source) {
    Mesh& dst = batch.mesh;
    batch.triSource.insert(batch.triSource.end(), static_cast<std::size_t>(src.TriangleCount()), source);
    const int base = dst.VertexCount();

    for (std::size_t i = 0; i + 2 < src.positions.size(); i += 3) {
        for (int a = 0; a < 3; ++a) {
            const float v = src.positions[i + static_cast<std::size_t>(a)];
            if (v < batch.boundsMin[a]) batch.boundsMin[a] = v;
            if (v > batch.boundsMax[a]) batch.boundsMax[a] = v;
        }
    }

    dst.positions.insert(dst.positions.end(), src.positions.begin(), src.positions.end());
    dst.normals.insert(dst.normals.end(), src.normals.begin(), src.normals.end());
    dst.colours.insert(dst.colours.end(), src.colours.begin(), src.colours.end());

    // UVs must stay aligned with the vertex count even when a merged-in mesh
    // has none, or every later mesh in the batch samples the wrong texel.
    if (!src.uvs.empty()) {
        dst.uvs.insert(dst.uvs.end(), src.uvs.begin(), src.uvs.end());
    } else {
        dst.uvs.insert(dst.uvs.end(), static_cast<std::size_t>(src.VertexCount()) * 2, 0.0f);
    }
    // Tangents likewise (w = 0 marks "none": no normal map is applied there).
    if (src.tangents.size() == static_cast<std::size_t>(src.VertexCount()) * 4) {
        dst.tangents.insert(dst.tangents.end(), src.tangents.begin(), src.tangents.end());
    } else {
        dst.tangents.insert(dst.tangents.end(), static_cast<std::size_t>(src.VertexCount()) * 4, 0.0f);
    }

    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (uint16_t i : src.indices) {
        dst.indices.push_back(static_cast<uint16_t>(base + i));
    }
}

// Copies `src` into world space using `world`.
Mesh ToWorldSpace(const Mesh& src, const core::Mat4& world) {
    Mesh out;
    out.name = src.name;
    out.materialIndex = src.materialIndex;
    out.sourceMeshIndex = src.sourceMeshIndex;

    const int n = src.VertexCount();
    out.positions.resize(static_cast<std::size_t>(n) * 3);
    out.normals.resize(static_cast<std::size_t>(n) * 3);
    out.uvs = src.uvs;
    out.indices = src.indices;
    const bool haveTangents = src.tangents.size() == static_cast<std::size_t>(n) * 4;
    if (haveTangents) out.tangents.resize(static_cast<std::size_t>(n) * 4);
    // A mirrored node flips the handedness: Godot builds the binormal in model
    // space and turns it with the node, which is cross(N', T') * w * sign(det).
    const float* w = world.m;
    const float det = w[0] * (w[5] * w[10] - w[9] * w[6]) - w[4] * (w[1] * w[10] - w[9] * w[2]) +
                      w[8] * (w[1] * w[6] - w[5] * w[2]);
    const float handedness = det < 0.0f ? -1.0f : 1.0f;

    for (int v = 0; v < n; ++v) {
        const std::size_t i3 = static_cast<std::size_t>(v) * 3;
        core::TransformPoint(world, &src.positions[i3], &out.positions[i3]);

        if (src.normals.size() == src.positions.size()) {
            // Direction, not point: a normal run through the translation picks
            // up the node's position and the shading fails in a way that reads
            // as bad source art.
            core::TransformDirection(world, &src.normals[i3], &out.normals[i3]);
            core::Normalize3(&out.normals[i3]);
        } else {
            out.normals[i3 + 0] = 0.0f;
            out.normals[i3 + 1] = 0.0f;
            out.normals[i3 + 2] = 1.0f;
        }
        if (haveTangents) {
            const std::size_t i4 = static_cast<std::size_t>(v) * 4;
            core::TransformDirection(world, &src.tangents[i4], &out.tangents[i4]);
            core::Normalize3(&out.tangents[i4]);
            out.tangents[i4 + 3] = src.tangents[i4 + 3] * handedness;
        }
    }
    return out;
}

}  // namespace

// Derives every stat from both containers, so a room loaded from a file counts
// its props' triangles as well as its batches', whichever was adopted first.
void Room::RecomputeStats() {
    stats_ = RoomStats{};
    stats_.batches = static_cast<int>(batches_.size());
    stats_.props   = static_cast<int>(props_.size());
    for (const RoomBatch& b : batches_) {
        stats_.triangles += b.mesh.TriangleCount();
        stats_.primitivesMerged += 1;
    }
    for (const RoomProp& p : props_) {
        stats_.triangles += p.mesh.TriangleCount();
    }
}

void Room::AdoptBatches(std::vector<RoomBatch>&& batches) {
    batches_ = std::move(batches);
    RecomputeStats();
}

int RoomProp::VariantTexture(const float albedo[4], const float emission[3], uint32_t textureKey) const {
    return FindVariantTexture(variants, albedo, emission, textureKey);
}

uint32_t TextureKeyHash(std::string_view key) {
    if (key.empty()) return 0u;
    uint32_t h = 2166136261u;
    for (const char c : key) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h == 0u ? 1u : h;
}

int FindVariantTexture(const std::vector<PropVariant>& variants, const float albedo[4],
                       const float emission[3], uint32_t textureKey) {
    // The keys are the export's own floats on both sides, so they agree to the
    // bit; the tolerance only absorbs a float round trip. Alpha is not part of
    // the key: it reaches the screen as the draw alpha, and the CRT icons fade
    // their albedo alpha while showing either variant (anim_icon fade).
    constexpr float kEps = 1e-4f;
    for (const PropVariant& v : variants) {
        bool same = true;
        for (int c = 0; c < 3 && same; ++c) same = std::fabs(v.albedo[c] - albedo[c]) <= kEps;
        for (int c = 0; c < 3 && same; ++c) same = std::fabs(v.emission[c] - emission[c]) <= kEps;
        if (same && v.textureKey == textureKey) return v.texture;
    }
    return -1;
}

namespace {

// Swapping with an empty vector releases the capacity (clear() keeps it).
template <typename T>
void Release(std::vector<T>& v) { std::vector<T>().swap(v); }

void ReleaseVertices(Mesh& m) {
    Release(m.positions);
    Release(m.normals);
    Release(m.uvs);
    Release(m.tangents);
    Release(m.uv2);
    Release(m.colours);
    Release(m.joints);
    Release(m.weights);
    Release(m.indices);
}

}  // namespace

void Room::ReleaseGeometry() {
    for (RoomBatch& b : batches_) ReleaseVertices(b.mesh);
    for (RoomProp& p : props_) ReleaseVertices(p.mesh);
}

void Room::AdoptProps(std::vector<RoomProp>&& props) {
    props_ = std::move(props);
    RecomputeStats();
}

int Room::AddTemplateProps(const std::string& templateKey, const std::string& meshName,
                           const MeshIndex& index, const core::Mat4& lightingWorld,
                           const Scene& scene, const RoomBuildOptions& options,
                           const std::vector<std::string>& surfaceTextures) {
    const MeshRef ref = index.Resolve(meshName);
    if (!ref.Valid()) return 0;
    Model model;
    const std::string path = options.modelDir + "/" + ref.glb;
    if (LoadGlb(path.c_str(), model) != GlbError::None) return 0;
    const std::vector<BakedLight> lights = CollectLights(scene);

    int surface = -1;
    int added = 0;
    for (const Mesh& src : model.meshes) {
        if (src.sourceMeshIndex != ref.meshIndex) continue;
        ++surface;
        RoomProp prop;
        prop.mesh = src;   // stays in MODEL space
        prop.templateKey = templateKey;
        prop.sourceGlb = ref.glb;
        prop.materialIndex = src.materialIndex;
        const std::size_t si = static_cast<std::size_t>(surface);
        if (si < surfaceTextures.size() && !surfaceTextures[si].empty()) {
            prop.sourceGlb = surfaceTextures[si];   // an inline texture key
            prop.materialIndex = 0;
        }
        prop.surface = surface;
        prop.node = -1;
        prop.bakeWorld = lightingWorld;

        Mesh lit = ToWorldSpace(src, lightingWorld);
        BakeVertexLighting(lit, core::Mat4{}, lights, options.bake);
        prop.mesh.colours = lit.colours;

        stats_.triangles += prop.mesh.TriangleCount();
        props_.push_back(std::move(prop));
        ++added;
    }
    stats_.props = static_cast<int>(props_.size());
    return added;
}

namespace {

// Adds `nodeIndex` and every ancestor to the table, so a prop's world transform
// can be composed through the live hierarchy. Returns the table index.
//
// Ancestors are added FIRST (the recursion unwinds parent-before-child), which
// is what lets TransformCache::Recompute be a single forward pass.
int AddChain(const Scene& scene, int nodeIndex, NodeTable& table) {
    if (nodeIndex < 0) return -1;
    const SceneNode& n = scene.Nodes()[static_cast<std::size_t>(nodeIndex)];

    const int existing = table.Find(n.path);
    if (existing >= 0) return existing;

    const int parent = AddChain(scene, n.parent, table);
    return table.Add(n.path, parent, n.local, n.visible);
}

}  // namespace

bool Room::Build(const Scene& scene, const MeshIndex& index,
                 const RoomBuildOptions& options) {
    batches_.clear();
    props_.clear();
    sources_.clear();
    nodes_.Clear();
    stats_ = RoomStats{};

    const std::vector<BakedLight> lights = CollectLights(scene);

    // Models are cached: the room instances the same file many times, and
    // reloading a 3 MB .glb per instance would dominate load time.
    std::unordered_map<std::string, Model> loaded;

    // Merge key is (file, material): the material becomes the texture bind,
    // and keeping the file in the key means a batch never spans two models,
    // which keeps the mapping debuggable.
    struct BatchKey {
        std::string glb;
        int material;
        bool operator==(const BatchKey& o) const {
            return material == o.material && glb == o.glb;
        }
    };
    struct BatchKeyHash {
        std::size_t operator()(const BatchKey& k) const {
            return std::hash<std::string>{}(k.glb) ^
                   (static_cast<std::size_t>(k.material + 1) * 2654435761u);
        }
    };
    // Index of the batch currently accepting geometry for this key. A key gets
    // a new batch once the previous one fills up.
    std::unordered_map<BatchKey, int, BatchKeyHash> open;

    for (const SceneNode& node : scene.Nodes()) {
        if (!node.HasMesh()) continue;
        ++stats_.nodesConsidered;

        // Classify by the INTRINSIC property first, then by visibility.
        //
        // The order also matters for reporting: 11 of the inline
        // primitives are also hidden, so testing visibility first files them
        // under `invisible` and inlinePrimitives reads 65 instead of 76. That
        // number tracks a real pipeline gap, and a metric that quietly
        // under-reports the thing it exists to measure is worse than no metric.
        if (node.IsInlinePrimitive()) {
            // A scene-authored PlaneMesh/BoxMesh with no counterpart in the
            // model library. The fallback library supplies the geometry Godot
            // itself tessellated; without it these are holes in the room.
            ++stats_.inlinePrimitives;

            const InlineMesh* im =
                options.inlineMeshes ? options.inlineMeshes->Find(node.path)
                                     : nullptr;
            if (!im) {
                // No geometry for it: a hole in the room, and a bucket of its
                // own so the accounting still closes.
                ++stats_.inlineMissing;
                continue;
            }

            const bool keepHiddenInline = options.keepHiddenAsProps && !node.visibleInTree;
            if (options.skipInvisible && !node.visibleInTree && !keepHiddenInline) {
                ++stats_.invisible;
                continue;
            }

            const int selfIdx = static_cast<int>(&node - scene.Nodes().data());

            if (options.filter.Enabled()) {
                const core::Mat4& w = scene.WorldTransform(selfIdx);
                const float origin[3] = {w.Tx(), w.Ty(), w.Tz()};
                if (!options.filter.Contains(origin)) {
                    ++stats_.outsideFilter;
                    continue;
                }
            }

            const core::Mat4& world = scene.WorldTransform(selfIdx);
            const bool inlineIsProp =
                (options.animated && options.animated->Covers(node.path)) ||
                keepHiddenInline ||
                (options.notDrawnAtLoad && options.notDrawnAtLoad->count(node.path) != 0) ||
                (options.scriptDriven && options.scriptDriven->count(node.path) != 0);

            for (std::size_t si = 0; si < im->surfaces.size(); ++si) {
                const Mesh& src = im->surfaces[si];
                const InlineMaterial mat =
                    si < im->materials.size() ? im->materials[si] : InlineMaterial{};
                // Keyed by the texture file, which the texture bake decodes;
                // untextured inline geometry keeps the shared "(inline)" key.
                const bool textured = !mat.textureFile.empty();
                const std::string texKey = textured ? mat.textureFile : std::string("(inline)");
                const int texMaterial = textured ? 0 : -1;
                // Godot adds emission on top of the lit albedo, so it goes on
                // top of the baked light, clamped as the bake clamps. The
                // health counter's icons and lines are white albedo emitting
                // green.
                const auto addEmission = [&mat](std::vector<uint8_t>& colours) {
                    for (std::size_t c = 0; c + 3 < colours.size(); c += 4) {
                        for (std::size_t k = 0; k < 3; ++k) {
                            const float v = static_cast<float>(colours[c + k]) +
                                            mat.emission[k] * 255.0f + 0.5f;
                            colours[c + k] = static_cast<uint8_t>(v > 255.0f ? 255.0f : v);
                        }
                    }
                };
                // An alpha-transparent surface needs the blended pass, which
                // only props reach: merged, its texture alpha is ignored and a
                // cut-out icon draws as its whole quad.
                if (inlineIsProp || mat.transparent) {
                    RoomProp prop;
                    prop.mesh = src;
                    prop.nodePath = node.path;
                    prop.sourceGlb = texKey;
                    prop.materialIndex = texMaterial;
                    prop.node = AddChain(scene, selfIdx, nodes_);
                    prop.visibleAtLoad = node.visibleInTree;
                    prop.ownVisible = node.visible;
                    prop.surface = static_cast<int>(si);
                    prop.bakeWorld = world;

                    Mesh lit = ToWorldSpace(src, world);
                    BakeVertexLighting(lit, core::Mat4{}, lights, options.bake);
                    prop.mesh.colours = lit.colours;
                    // No emission here: the renderer adds a prop's live from its
                    // node's material (u_emission); baking it too would count it
                    // twice.
                    for (std::size_t i = 0; i + 2 < lit.positions.size(); i += 3) {
                        for (int a = 0; a < 3; ++a) {
                            const float v = lit.positions[i + static_cast<std::size_t>(a)];
                            if (v < prop.boundsMin[a]) prop.boundsMin[a] = v;
                            if (v > prop.boundsMax[a]) prop.boundsMax[a] = v;
                        }
                    }
                    props_.push_back(std::move(prop));
                    ++stats_.props;
                } else {
                    Mesh worldMesh = ToWorldSpace(src, world);
                    BakeVertexLighting(worldMesh, core::Mat4{}, lights, options.bake);
                    addEmission(worldMesh.colours);

                    // Untextured inline geometry shares one batch key rather
                    // than fragmenting the merge; a textured surface merges
                    // per texture, which becomes its bind.
                    const BatchKey key{texKey, texMaterial};
                    auto slot = open.find(key);
                    if (slot != open.end()) {
                        RoomBatch& batch = batches_[static_cast<std::size_t>(slot->second)];
                        if (batch.mesh.VertexCount() + worldMesh.VertexCount() >
                            options.maxVerticesPerBatch) {
                            slot = open.end();
                        }
                    }
                    if (slot == open.end()) {
                        RoomBatch batch;
                        batch.materialIndex = texMaterial;
                        batch.sourceGlb = texKey;
                        batches_.push_back(std::move(batch));
                        open[key] = static_cast<int>(batches_.size()) - 1;
                        slot = open.find(key);
                    }
                    sources_.push_back({node.path, static_cast<int>(si)});
                    AppendMesh(batches_[static_cast<std::size_t>(slot->second)], worldMesh,
                               static_cast<int32_t>(sources_.size()) - 1);
                    ++stats_.primitivesMerged;
                }
            }
            ++stats_.inlineDrawn;
            ++stats_.nodesDrawn;
            continue;
        }
        // An ANIMATED node is kept even when it is hidden at load: it has its
        // own draw call and a runtime visibility flag, so the game can show it
        // when it is used. Static geometry is merged and has no such handle,
        // so it is filtered here or never.
        const bool nodeIsAnimated =
            options.animated && options.animated->Covers(node.path);
        const bool keepHidden = options.keepHiddenAsProps && !node.visibleInTree;
        if (options.skipInvisible && !node.visibleInTree && !nodeIsAnimated && !keepHidden) {
            ++stats_.invisible;
            continue;
        }

        const MeshRef ref = index.Resolve(node.meshId);
        if (!ref.Valid()) {
            ++stats_.unresolved;
            continue;
        }

        // Area filter, tested on the node's world ORIGIN. Testing the geometry
        // instead would need the model loaded first, which defeats the point --
        // the filter exists so far-away scenes are never read from disk at all.
        if (options.filter.Enabled()) {
            const core::Mat4& w =
                scene.WorldTransform(static_cast<int>(&node - scene.Nodes().data()));
            const float origin[3] = {w.Tx(), w.Ty(), w.Tz()};
            if (!options.filter.Contains(origin)) {
                ++stats_.outsideFilter;
                continue;
            }
        }

        auto it = loaded.find(ref.glb);
        if (it == loaded.end()) {
            Model model;
            const std::string path = options.modelDir + "/" + ref.glb;
            const GlbError err = LoadGlb(path.c_str(), model);
            if (err != GlbError::None) {
                std::printf("room: %s: %s\n", ref.glb.c_str(), GlbErrorText(err));
                ++stats_.unresolved;
                continue;
            }
            ++stats_.modelsLoaded;
            it = loaded.emplace(ref.glb, std::move(model)).first;
        }
        const Model& model = it->second;

        const core::Mat4& world =
            scene.WorldTransform(static_cast<int>(&node - scene.Nodes().data()));

        // Animated nodes are held OUT of the merge. Merging them would bake
        // their vertices to world space and leave nothing for animation to
        // move.
        // ...and so is geometry Godot does not draw at load: its state lives in
        // the runtime tree, and only a prop can read it.
        const bool notDrawn =
            options.notDrawnAtLoad && options.notDrawnAtLoad->count(node.path) != 0;
        // ...and so is geometry a script recolours: BlinkerBranch writes its
        // parent's surface 0, which merged geometry has no handle for.
        const bool scripted =
            options.scriptDriven && options.scriptDriven->count(node.path) != 0;
        const bool isProp = nodeIsAnimated || keepHidden || notDrawn || scripted;

        bool drewAnything = false;
        // The primitive's index within this glTF mesh: Godot imports primitives
        // as surfaces in order, so this is the surface an override targets.
        int surface = -1;
        for (const Mesh& src : model.meshes) {
            // One glTF mesh can hold several primitives; all of them belong to
            // this node.
            if (src.sourceMeshIndex != ref.meshIndex) continue;
            ++surface;

            if (isProp) {
                RoomProp prop;
                prop.mesh = src;                  // stays in MODEL space
                prop.nodePath = node.path;
                prop.sourceGlb = ref.glb;
                prop.materialIndex = src.materialIndex;
                prop.surface = surface;

                // Register this node and its whole ancestor chain, so animating
                // any of them moves the prop. Animation usually drives a parent
                // ("player shoot dealer" moves `shotgun main parent`, not the
                // individual shotgun meshes).
                const int selfIndex =
                    static_cast<int>(&node - scene.Nodes().data());
                prop.node = AddChain(scene, selfIndex, nodes_);
                prop.visibleAtLoad = node.visibleInTree;
                prop.ownVisible = node.visible;
                prop.bakeWorld = world;

                // Light it at its rest pose, in world space, then keep the
                // model-space vertices.
                Mesh lit = ToWorldSpace(src, world);
                BakeVertexLighting(lit, core::Mat4{}, lights, options.bake);
                prop.mesh.colours = lit.colours;

                for (std::size_t i = 0; i + 2 < lit.positions.size(); i += 3) {
                    for (int a = 0; a < 3; ++a) {
                        const float v = lit.positions[i + static_cast<std::size_t>(a)];
                        if (v < prop.boundsMin[a]) prop.boundsMin[a] = v;
                        if (v > prop.boundsMax[a]) prop.boundsMax[a] = v;
                    }
                }

                props_.push_back(std::move(prop));
                ++stats_.props;
                drewAnything = true;
                continue;
            }

            Mesh worldMesh = ToWorldSpace(src, world);
            // Lights are in world space, and so is the geometry now, so the
            // bake transform is identity.
            BakeVertexLighting(worldMesh, core::Mat4{}, lights, options.bake);

            const BatchKey key{ref.glb, src.materialIndex};
            auto slot = open.find(key);

            if (slot != open.end()) {
                RoomBatch& batch = batches_[static_cast<std::size_t>(slot->second)];
                if (batch.mesh.VertexCount() + worldMesh.VertexCount() >
                    options.maxVerticesPerBatch) {
                    slot = open.end();      // full; start a new one below
                }
            }

            if (slot == open.end()) {
                RoomBatch batch;
                batch.materialIndex = src.materialIndex;
                batch.sourceGlb = ref.glb;
                batches_.push_back(std::move(batch));
                open[key] = static_cast<int>(batches_.size()) - 1;
                slot = open.find(key);
            }

            sources_.push_back({node.path, surface});
            AppendMesh(batches_[static_cast<std::size_t>(slot->second)], worldMesh,
                       static_cast<int32_t>(sources_.size()) - 1);
            ++stats_.primitivesMerged;
            drewAnything = true;
        }

        if (drewAnything) ++stats_.nodesDrawn;
        else ++stats_.unresolved;
    }

    stats_.batches = static_cast<int>(batches_.size());
    // Props count too. Leaving them out would under-report the room's real
    // cost by exactly the geometry that moves, which is the part most likely
    // to be on screen.
    for (const RoomBatch& b : batches_) stats_.triangles += b.mesh.TriangleCount();
    for (const RoomProp& p : props_)    stats_.triangles += p.mesh.TriangleCount();

    // A room of props alone is a room: the menu's geometry all moves or blends.
    return !batches_.empty() || !props_.empty();
}

}  // namespace assets
