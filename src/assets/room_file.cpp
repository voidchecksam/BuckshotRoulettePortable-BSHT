#include "assets/room_file.h"

#include "core/mat4.h"

#include <cstddef>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace assets {

namespace {

constexpr char kMagic[4] = {'B', 'S', 'R', 'M'};

// The on-disk header. Fixed size, little-endian.
struct FileHeader {
    char     magic[4];
    uint32_t version;
    uint32_t batchCount;
    uint32_t propCount;
    uint32_t nodeCount;
    uint32_t totalVertices;   // across batches AND props
    uint32_t totalIndices;    // across batches AND props
    uint32_t textureCount;
    uint32_t texturePixelBytes;
};

// One entry of the transform hierarchy, followed by its path bytes.
struct NodeRecord {
    int32_t  parent;
    uint32_t pathLength;
    float    restLocal[16];
    uint32_t visible;          // the node's OWN flag (v6)
};

struct BatchRecord {
    uint32_t vertexCount;
    uint32_t indexCount;
    int32_t  materialIndex;
    int32_t  texture;          // index into the pool, or -1
    float    boundsMin[3];
    float    boundsMax[3];
};

// Followed by `glbLength` bytes of source name. The name plus materialIndex is
// the join key a batch uses to find this entry, and it is kept so a bake can be
// checked later.
struct TextureRecord {
    uint32_t width;
    uint32_t height;
    uint32_t offset;           // into the shared pixel pool
    uint32_t bytes;
    int32_t  materialIndex;
    uint32_t glbLength;
    uint32_t format;           // TextureFormat (v8)
};

// Followed by `nodePathLength` bytes of path, `templateKeyLength` bytes of
// template key (v7), `variantCount` VariantRecords (v9), then vertices, then
// indices.
struct PropRecord {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t nodePathLength;
    int32_t  materialIndex;
    int32_t  texture;           // index into the pool, or -1
    int32_t  node;              // index into the node table
    uint32_t visibleAtLoad;     // 0 = starts hidden, shown when used
    uint32_t ownVisible;        // the node's own flag, without ancestors (v6)
    float    boundsMin[3];
    float    boundsMax[3];
    int32_t  surface;           // primitive index within the node's mesh (v7)
    uint32_t templateKeyLength; // 0 for a placed prop (v7)
    uint32_t variantCount;      // VariantRecords after the template key (v9)
    float    bakePivot[3];      // where its texels were lit (v10)
    uint32_t hasBakePivot;      // 0: never rescaled by the probe ratio
    uint32_t skinned;           // 1: 4 joints + 4 weights (u8) per vertex follow the indices (v12)
    float    bakeGrade[2];      // brightness, contrast its texels were baked at; 0: the scene's (v13)
};

// After the last prop (v10): the probe grid, then dims[0]*dims[1]*dims[2]*3
// floats of linear RGB. All dims 0 when the room has none.
struct ProbeRecord {
    float   origin[3];
    float   step;
    int32_t dims[3];
};

// One material a script can swap onto the prop's surface, and its page (v9).
struct VariantRecord {
    float   albedo[4];
    float   emission[3];
    int32_t texture;
    uint32_t textureKey;   // TextureKeyHash of the albedo texture (v11)
};

static_assert(sizeof(RoomFileVertex) == 36,
              "vertex layout changed; bump kRoomFileVersion");
static_assert(sizeof(FileHeader) == 36, "header layout changed");
static_assert(sizeof(BatchRecord) == 40, "batch record layout changed");
static_assert(sizeof(PropRecord) == 96, "prop record layout changed");   // v13: + bake grade
static_assert(sizeof(ProbeRecord) == 28, "probe record layout changed");
static_assert(sizeof(VariantRecord) == 36, "variant record layout changed");
static_assert(sizeof(TextureRecord) == 28, "texture record layout changed");
static_assert(sizeof(NodeRecord) == 76, "node record layout changed");

// Packs a mesh's vertices into the on-disk layout.
void PackVertices(const Mesh& m, std::vector<RoomFileVertex>& out) {
    const int n = m.VertexCount();
    out.clear();
    out.resize(static_cast<std::size_t>(n));

    const bool haveUvs  = m.uvs.size()     >= static_cast<std::size_t>(n) * 2;
    const bool haveCols = m.colours.size() >= static_cast<std::size_t>(n) * 4;
    const bool haveNrms = m.normals.size() >= static_cast<std::size_t>(n) * 3;

    for (int v = 0; v < n; ++v) {
        RoomFileVertex& o = out[static_cast<std::size_t>(v)];
        const std::size_t i3 = static_cast<std::size_t>(v) * 3;
        o.pos[0] = m.positions[i3 + 0];
        o.pos[1] = m.positions[i3 + 1];
        o.pos[2] = m.positions[i3 + 2];
        o.nrm[0] = haveNrms ? m.normals[i3 + 0] : 0.0f;
        o.nrm[1] = haveNrms ? m.normals[i3 + 1] : 0.0f;
        o.nrm[2] = haveNrms ? m.normals[i3 + 2] : 1.0f;
        o.uv[0] = haveUvs ? m.uvs[static_cast<std::size_t>(v) * 2 + 0] : 0.0f;
        o.uv[1] = haveUvs ? m.uvs[static_cast<std::size_t>(v) * 2 + 1] : 0.0f;
        if (haveCols) {
            const std::size_t i4 = static_cast<std::size_t>(v) * 4;
            o.col[0] = m.colours[i4 + 0];
            o.col[1] = m.colours[i4 + 1];
            o.col[2] = m.colours[i4 + 2];
            o.col[3] = m.colours[i4 + 3];
        } else {
            o.col[0] = o.col[1] = o.col[2] = o.col[3] = 255;
        }
    }
}

// Unpacks into a Mesh.
void UnpackVertices(const std::vector<RoomFileVertex>& in, Mesh& m) {
    const std::size_t n = in.size();
    m.positions.resize(n * 3);
    m.normals.resize(n * 3);
    m.uvs.resize(n * 2);
    m.colours.resize(n * 4);
    for (std::size_t v = 0; v < n; ++v) {
        const RoomFileVertex& s = in[v];
        m.positions[v * 3 + 0] = s.pos[0];
        m.positions[v * 3 + 1] = s.pos[1];
        m.positions[v * 3 + 2] = s.pos[2];
        m.normals[v * 3 + 0] = s.nrm[0];
        m.normals[v * 3 + 1] = s.nrm[1];
        m.normals[v * 3 + 2] = s.nrm[2];
        m.uvs[v * 2 + 0] = s.uv[0];
        m.uvs[v * 2 + 1] = s.uv[1];
        m.colours[v * 4 + 0] = s.col[0];
        m.colours[v * 4 + 1] = s.col[1];
        m.colours[v * 4 + 2] = s.col[2];
        m.colours[v * 4 + 3] = s.col[3];
    }
}

void WriteBounds(const float lo[3], const float hi[3], bool valid,
                 float outLo[3], float outHi[3]) {
    for (int i = 0; i < 3; ++i) {
        // Sentinel bounds would read back as an AABB containing everything.
        outLo[i] = valid ? lo[i] : 0.0f;
        outHi[i] = valid ? hi[i] : 0.0f;
    }
}

}  // namespace

const std::size_t RoomFileOffsets::kVersion = offsetof(FileHeader, version);
const std::size_t RoomFileOffsets::kTotalVertices =
    offsetof(FileHeader, totalVertices);

const char* RoomFileErrorText(RoomFileError e) {
    switch (e) {
    case RoomFileError::None:         return "ok";
    case RoomFileError::FileNotFound: return "file not found";
    case RoomFileError::TooSmall:     return "file smaller than a header";
    case RoomFileError::BadMagic:     return "not a .room file";
    case RoomFileError::BadVersion:   return "wrong .room version; re-bake";
    case RoomFileError::Truncated:    return "file ends mid-record";
    case RoomFileError::Inconsistent: return "header counts disagree with contents";
    }
    return "unknown error";
}

RoomFileStats MeasureRoom(const Room& room) {
    RoomFileStats s{};
    s.batches = static_cast<uint32_t>(room.Batches().size());
    s.props   = static_cast<uint32_t>(room.Props().size());
    s.nodes   = static_cast<uint32_t>(room.Nodes().Size());

    uint64_t pathBytes = 0;
    for (const RoomBatch& b : room.Batches()) {
        s.vertices += static_cast<uint32_t>(b.mesh.VertexCount());
        s.indices  += static_cast<uint32_t>(b.mesh.indices.size());
    }
    for (const RoomProp& p : room.Props()) {
        s.vertices += static_cast<uint32_t>(p.mesh.VertexCount());
        s.indices  += static_cast<uint32_t>(p.mesh.indices.size());
        pathBytes  += p.nodePath.size() + p.templateKey.size() +
                      p.variants.size() * sizeof(VariantRecord) + p.mesh.joints.size() * 2;   // + skin (v12)
    }

    uint64_t nodeBytes = 0;
    for (const TransformNode& n : room.Nodes().Nodes()) {
        nodeBytes += sizeof(NodeRecord) + n.path.size();
    }

    s.bytes = sizeof(FileHeader) +
              static_cast<uint64_t>(s.batches) * sizeof(BatchRecord) +
              static_cast<uint64_t>(s.props)   * sizeof(PropRecord) +
              nodeBytes +
              pathBytes +
              static_cast<uint64_t>(s.vertices) * sizeof(RoomFileVertex) +
              static_cast<uint64_t>(s.indices)  * sizeof(uint16_t) +
              sizeof(ProbeRecord) + room.Probes().rgb.size() * sizeof(float);
    return s;
}

bool WriteRoomFile(const char* path, const Room& room) {
    // A pool read back from a file holds a buffer per texture and no shared pool
    // (TexturePool::Chunked); writing it would drop every pixel. Only bakes write.
    if (room.Textures().Chunked()) return false;
    if (!path) return false;
    if (room.Batches().empty() && room.Props().empty()) return false;

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    const RoomFileStats stats = MeasureRoom(room);

    FileHeader h{};
    std::memcpy(h.magic, kMagic, 4);
    h.version = kRoomFileVersion;
    h.batchCount = stats.batches;
    h.propCount = stats.props;
    h.nodeCount = stats.nodes;
    h.totalVertices = stats.vertices;
    h.totalIndices = stats.indices;
    h.textureCount = static_cast<uint32_t>(room.Textures().Size());
    h.texturePixelBytes = static_cast<uint32_t>(room.Textures().ByteSize());
    std::fwrite(&h, sizeof(h), 1, f);

    // Textures first: a batch's texture index must be resolvable as soon as
    // batches are read back.
    for (const BakedTexture& t : room.Textures().Textures()) {
        TextureRecord r{};
        r.width = static_cast<uint32_t>(t.width);
        r.height = static_cast<uint32_t>(t.height);
        r.offset = t.offset;
        r.bytes = t.bytes;
        r.materialIndex = t.materialIndex;
        r.glbLength = static_cast<uint32_t>(t.sourceGlb.size());
        r.format = static_cast<uint32_t>(t.format);
        std::fwrite(&r, sizeof(r), 1, f);
        std::fwrite(t.sourceGlb.data(), 1, t.sourceGlb.size(), f);
    }
    if (!room.Textures().Pixels().empty()) {
        std::fwrite(room.Textures().Pixels().data(), 1,
                    room.Textures().Pixels().size(), f);
    }

    std::vector<RoomFileVertex> verts;

    for (const RoomBatch& b : room.Batches()) {
        BatchRecord r{};
        r.vertexCount = static_cast<uint32_t>(b.mesh.VertexCount());
        r.indexCount = static_cast<uint32_t>(b.mesh.indices.size());
        r.materialIndex = b.materialIndex;
        r.texture = b.texture;
        WriteBounds(b.boundsMin, b.boundsMax, b.HasBounds(),
                    r.boundsMin, r.boundsMax);
        std::fwrite(&r, sizeof(r), 1, f);

        PackVertices(b.mesh, verts);
        std::fwrite(verts.data(), sizeof(RoomFileVertex), verts.size(), f);
        std::fwrite(b.mesh.indices.data(), sizeof(uint16_t),
                    b.mesh.indices.size(), f);
    }

    // Nodes first: a prop's node index must already be resolvable when props
    // are read back.
    for (const TransformNode& n : room.Nodes().Nodes()) {
        NodeRecord r{};
        r.parent = n.parent;
        r.pathLength = static_cast<uint32_t>(n.path.size());
        for (int i = 0; i < 16; ++i) r.restLocal[i] = n.restLocal.m[i];
        r.visible = n.visible ? 1u : 0u;
        std::fwrite(&r, sizeof(r), 1, f);
        std::fwrite(n.path.data(), 1, n.path.size(), f);
    }

    for (const RoomProp& p : room.Props()) {
        PropRecord r{};
        r.vertexCount = static_cast<uint32_t>(p.mesh.VertexCount());
        r.indexCount = static_cast<uint32_t>(p.mesh.indices.size());
        r.nodePathLength = static_cast<uint32_t>(p.nodePath.size());
        r.materialIndex = p.materialIndex;
        r.texture = p.texture;
        r.node = p.node;
        r.visibleAtLoad = p.visibleAtLoad ? 1u : 0u;
        r.ownVisible = p.ownVisible ? 1u : 0u;
        r.surface = p.surface;
        r.templateKeyLength = static_cast<uint32_t>(p.templateKey.size());
        r.variantCount = static_cast<uint32_t>(p.variants.size());
        r.hasBakePivot = p.hasBakePivot ? 1u : 0u;
        const bool skinned = p.mesh.joints.size() == p.mesh.positions.size() / 3 * 4 && !p.mesh.joints.empty() &&
                             p.mesh.weights.size() == p.mesh.joints.size();
        r.skinned = skinned ? 1u : 0u;
        for (int a = 0; a < 3; ++a) r.bakePivot[a] = p.bakePivot[a];
        r.bakeGrade[0] = p.bakeBrightness;
        r.bakeGrade[1] = p.bakeContrast;
        WriteBounds(p.boundsMin, p.boundsMax, p.HasBounds(),
                    r.boundsMin, r.boundsMax);
        std::fwrite(&r, sizeof(r), 1, f);
        std::fwrite(p.nodePath.data(), 1, p.nodePath.size(), f);
        std::fwrite(p.templateKey.data(), 1, p.templateKey.size(), f);
        for (const PropVariant& v : p.variants) {
            VariantRecord vr{};
            for (int c = 0; c < 4; ++c) vr.albedo[c] = v.albedo[c];
            for (int c = 0; c < 3; ++c) vr.emission[c] = v.emission[c];
            vr.texture = v.texture;
            vr.textureKey = v.textureKey;
            std::fwrite(&vr, sizeof(vr), 1, f);
        }

        PackVertices(p.mesh, verts);
        std::fwrite(verts.data(), sizeof(RoomFileVertex), verts.size(), f);
        std::fwrite(p.mesh.indices.data(), sizeof(uint16_t),
                    p.mesh.indices.size(), f);
        if (skinned) {
            std::fwrite(p.mesh.joints.data(), 1, p.mesh.joints.size(), f);
            std::vector<uint8_t> w(p.mesh.weights.size());
            for (std::size_t k = 0; k < w.size(); ++k) {
                const float c = std::clamp(p.mesh.weights[k], 0.0f, 1.0f);
                w[k] = static_cast<uint8_t>(c * 255.0f + 0.5f);
            }
            std::fwrite(w.data(), 1, w.size(), f);
        }
    }

    const ProbeGrid& grid = room.Probes();
    ProbeRecord pr{};
    for (int a = 0; a < 3; ++a) {
        pr.origin[a] = grid.origin[a];
        pr.dims[a] = grid.Empty() ? 0 : grid.dims[a];
    }
    pr.step = grid.step;
    std::fwrite(&pr, sizeof(pr), 1, f);
    if (!grid.Empty()) std::fwrite(grid.rgb.data(), sizeof(float), grid.rgb.size(), f);

    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

RoomFileError ReadRoomFile(const char* path, Room& out,
                           const std::function<void(float)>& progress) {
    out = Room{};
    if (!path) return RoomFileError::FileNotFound;

    std::FILE* f = std::fopen(path, "rb");
    if (!f) return RoomFileError::FileNotFound;

    // Reported in steps of at least 2%: each call redraws the loading screen,
    // and a redraw per record would cost more than the read.
    long fileSize = 0;
    if (progress && std::fseek(f, 0, SEEK_END) == 0) {
        fileSize = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
    }
    float reported = -1.0f;
    const auto report = [&](bool force) {
        if (!progress || fileSize <= 0) return;
        const long at = std::ftell(f);
        float p = static_cast<float>(at) / static_cast<float>(fileSize);
        if (p > 1.0f) p = 1.0f;
        if (p < reported) p = reported;
        if (force || p - reported >= 0.02f) {
            reported = p;
            progress(p);
        }
    };
    report(true);

    FileHeader h{};
    if (std::fread(&h, sizeof(h), 1, f) != 1) {
        std::fclose(f);
        return RoomFileError::TooSmall;
    }
    if (std::memcmp(h.magic, kMagic, 4) != 0) {
        std::fclose(f);
        return RoomFileError::BadMagic;
    }
    if (h.version != kRoomFileVersion) {
        // Rejected, never reinterpreted: an older file read as a newer one
        // could load as a room where nothing moves, with no error.
        std::fclose(f);
        return RoomFileError::BadVersion;
    }

    // Textures first, matching the write order: a batch's texture index has to
    // be resolvable the moment batches are read.
    {
        std::vector<BakedTexture> textures;
        textures.reserve(h.textureCount);
        for (uint32_t i = 0; i < h.textureCount; ++i) {
        report(false);
            TextureRecord r{};
            if (std::fread(&r, sizeof(r), 1, f) != 1) {
                std::fclose(f);
                return RoomFileError::Truncated;
            }
            BakedTexture t;
            t.width = static_cast<int>(r.width);
            t.height = static_cast<int>(r.height);
            t.offset = r.offset;
            t.bytes = r.bytes;
            t.materialIndex = r.materialIndex;
            t.format = static_cast<TextureFormat>(r.format);
            if (r.glbLength > 0) {
                // A length field is attacker- and corruption-controlled; cap it
                // before it becomes an allocation.
                if (r.glbLength > (1u << 16)) {
                    std::fclose(f);
                    return RoomFileError::Inconsistent;
                }
                t.sourceGlb.resize(r.glbLength);
                if (std::fread(&t.sourceGlb[0], 1, r.glbLength, f) != r.glbLength) {
                    std::fclose(f);
                    return RoomFileError::Truncated;
                }
            }
            if (!t.Valid()) {
                std::fclose(f);
                return RoomFileError::Inconsistent;
            }
            textures.push_back(std::move(t));
        }

        // Each texture into a buffer of its own, in file order (TexturePool::Chunked):
        // one buffer for the whole pool could not find a hole in a fragmented heap.
        std::vector<std::size_t> order(textures.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&textures](std::size_t a, std::size_t b) { return textures[a].offset < textures[b].offset; });
        std::vector<std::vector<uint8_t>> chunks(textures.size());
        uint64_t at = 0;   // bytes of the pool read so far
        for (std::size_t i : order) {
            const BakedTexture& t = textures[i];
            if (t.offset < at || static_cast<uint64_t>(t.offset) + t.bytes > h.texturePixelBytes) {
                std::fclose(f);   // overlapping or outside the pool: not a file this writer made
                return RoomFileError::Inconsistent;
            }
            if (t.offset > at && std::fseek(f, static_cast<long>(t.offset - at), SEEK_CUR) != 0) {
                std::fclose(f);
                return RoomFileError::Truncated;
            }
            chunks[i].resize(t.bytes);
            if (t.bytes && std::fread(chunks[i].data(), 1, t.bytes, f) != t.bytes) {
                std::fclose(f);
                return RoomFileError::Truncated;
            }
            at = static_cast<uint64_t>(t.offset) + t.bytes;
        }
        if (at < h.texturePixelBytes &&
            std::fseek(f, static_cast<long>(h.texturePixelBytes - at), SEEK_CUR) != 0) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }
        out.Textures().ResetChunked(std::move(textures), std::move(chunks));
        if (!out.Textures().Consistent()) {
            // An entry whose range runs past the pool would be an out-of-bounds
            // read at upload time, on a device with no debugger.
            std::fclose(f);
            return RoomFileError::Inconsistent;
        }
    }

    std::vector<RoomBatch> batches;
    std::vector<RoomProp> props;
    NodeTable nodes;
    nodes.Reserve(h.nodeCount);
    batches.reserve(h.batchCount);
    props.reserve(h.propCount);

    uint32_t seenVertices = 0;
    uint32_t seenIndices = 0;
    std::vector<RoomFileVertex> verts;

    for (uint32_t i = 0; i < h.batchCount; ++i) {
        report(false);
        BatchRecord r{};
        if (std::fread(&r, sizeof(r), 1, f) != 1) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }

        RoomBatch b;
        b.materialIndex = r.materialIndex;
        b.texture = r.texture;
        for (int a = 0; a < 3; ++a) {
            b.boundsMin[a] = r.boundsMin[a];
            b.boundsMax[a] = r.boundsMax[a];
        }

        verts.assign(r.vertexCount, RoomFileVertex{});
        if (r.vertexCount &&
            std::fread(verts.data(), sizeof(RoomFileVertex), r.vertexCount, f) !=
                r.vertexCount) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }
        UnpackVertices(verts, b.mesh);

        b.mesh.indices.resize(r.indexCount);
        if (r.indexCount &&
            std::fread(b.mesh.indices.data(), sizeof(uint16_t), r.indexCount, f) !=
                r.indexCount) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }

        seenVertices += r.vertexCount;
        seenIndices += r.indexCount;
        batches.push_back(std::move(b));
    }

    for (uint32_t i = 0; i < h.nodeCount; ++i) {
        NodeRecord r{};
        if (std::fread(&r, sizeof(r), 1, f) != 1) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }
        std::string path(r.pathLength, '\0');
        if (r.pathLength &&
            std::fread(&path[0], 1, r.pathLength, f) != r.pathLength) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }
        core::Mat4 rest;
        for (int k = 0; k < 16; ++k) rest.m[k] = r.restLocal[k];
        nodes.Add(path, r.parent, rest, r.visible != 0u);
    }

    for (uint32_t i = 0; i < h.propCount; ++i) {
        report(false);
        PropRecord r{};
        if (std::fread(&r, sizeof(r), 1, f) != 1) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }

        RoomProp p;
        p.materialIndex = r.materialIndex;
        p.texture = r.texture;
        p.node = r.node;
        p.visibleAtLoad = r.visibleAtLoad != 0u;
        p.ownVisible = r.ownVisible != 0u;
        p.surface = r.surface;
        p.hasBakePivot = r.hasBakePivot != 0u;
        for (int a = 0; a < 3; ++a) p.bakePivot[a] = r.bakePivot[a];
        p.bakeBrightness = r.bakeGrade[0];
        p.bakeContrast = r.bakeGrade[1];
        for (int a = 0; a < 3; ++a) {
            p.boundsMin[a] = r.boundsMin[a];
            p.boundsMax[a] = r.boundsMax[a];
        }

        p.nodePath.resize(r.nodePathLength);
        if (r.nodePathLength &&
            std::fread(&p.nodePath[0], 1, r.nodePathLength, f) != r.nodePathLength) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }
        // A length field is corruption-controlled; cap it before it allocates.
        if (r.templateKeyLength > (1u << 16)) {
            std::fclose(f);
            return RoomFileError::Inconsistent;
        }
        p.templateKey.resize(r.templateKeyLength);
        if (r.templateKeyLength &&
            std::fread(&p.templateKey[0], 1, r.templateKeyLength, f) != r.templateKeyLength) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }
        // Corruption-controlled too: a handful of materials is all any
        // script swaps.
        if (r.variantCount > 64u) {
            std::fclose(f);
            return RoomFileError::Inconsistent;
        }
        p.variants.resize(r.variantCount);
        for (PropVariant& v : p.variants) {
            VariantRecord vr{};
            if (std::fread(&vr, sizeof(vr), 1, f) != 1) {
                std::fclose(f);
                return RoomFileError::Truncated;
            }
            for (int c = 0; c < 4; ++c) v.albedo[c] = vr.albedo[c];
            for (int c = 0; c < 3; ++c) v.emission[c] = vr.emission[c];
            v.texture = vr.texture;
            v.textureKey = vr.textureKey;
        }

        verts.assign(r.vertexCount, RoomFileVertex{});
        if (r.vertexCount &&
            std::fread(verts.data(), sizeof(RoomFileVertex), r.vertexCount, f) !=
                r.vertexCount) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }
        UnpackVertices(verts, p.mesh);

        p.mesh.indices.resize(r.indexCount);
        if (r.indexCount &&
            std::fread(p.mesh.indices.data(), sizeof(uint16_t), r.indexCount, f) !=
                r.indexCount) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }

        if (r.skinned) {
            const std::size_t n = static_cast<std::size_t>(r.vertexCount) * 4;
            std::vector<uint8_t> w(n);
            p.mesh.joints.resize(n);
            if (std::fread(p.mesh.joints.data(), 1, n, f) != n || std::fread(w.data(), 1, n, f) != n) {
                std::fclose(f);
                return RoomFileError::Truncated;
            }
            p.mesh.weights.resize(n);
            for (std::size_t k = 0; k < n; ++k) p.mesh.weights[k] = static_cast<float>(w[k]) / 255.0f;
        }

        seenVertices += r.vertexCount;
        seenIndices += r.indexCount;
        props.push_back(std::move(p));
    }

    ProbeRecord pr{};
    if (std::fread(&pr, sizeof(pr), 1, f) != 1) {
        std::fclose(f);
        return RoomFileError::Truncated;
    }
    ProbeGrid grid;
    // Corruption-controlled: the table's grid is a few hundred points a side.
    const bool anyDim = pr.dims[0] != 0 || pr.dims[1] != 0 || pr.dims[2] != 0;
    if (anyDim) {
        for (int a = 0; a < 3; ++a) {
            if (pr.dims[a] <= 0 || pr.dims[a] > 1024) {
                std::fclose(f);
                return RoomFileError::Inconsistent;
            }
        }
        grid.rgb.resize(static_cast<std::size_t>(pr.dims[0]) * pr.dims[1] * pr.dims[2] * 3);
        if (std::fread(grid.rgb.data(), sizeof(float), grid.rgb.size(), f) != grid.rgb.size()) {
            std::fclose(f);
            return RoomFileError::Truncated;
        }
    }
    for (int a = 0; a < 3; ++a) {
        grid.origin[a] = pr.origin[a];
        grid.dims[a] = pr.dims[a];
    }
    grid.step = pr.step;
    std::fclose(f);

    // The header totals are redundant with the records on purpose: a
    // disagreement means the writer did not understand the format.
    if (seenVertices != h.totalVertices || seenIndices != h.totalIndices) {
        return RoomFileError::Inconsistent;
    }

    out.MutableNodes() = std::move(nodes);
    out.AdoptBatches(std::move(batches));
    out.AdoptProps(std::move(props));
    out.SetProbes(std::move(grid));
    if (progress) {
        reported = 1.0f;
        progress(1.0f);
    }
    return RoomFileError::None;
}

}  // namespace assets
