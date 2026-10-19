#include "assets/glb.h"

#include "core/json_lite.h"

#include <cstdio>
#include <cstring>

namespace assets {

namespace {

using core::json::FindArray;
using core::json::FindObject;
using core::json::ReadInt;
using core::json::ReadString;
using core::json::Span;
using core::json::SplitObjects;

constexpr uint32_t kMagicGltf = 0x46546C67u;   // "glTF"
constexpr uint32_t kChunkJson = 0x4E4F534Au;   // "JSON"
constexpr uint32_t kChunkBin  = 0x004E4942u;   // "BIN\0"

constexpr int kModeTriangles = 4;

constexpr int kCompByte          = 5120;
constexpr int kCompUnsignedByte  = 5121;
constexpr int kCompShort         = 5122;
constexpr int kCompUnsignedShort = 5123;
constexpr int kCompUnsignedInt   = 5125;
constexpr int kCompFloat         = 5126;

// GLES2 has no 32-bit index path, so every mesh must fit here.
constexpr int kMaxVerticesPerPrimitive = 65535;

uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int ComponentByteSize(int componentType) {
    switch (componentType) {
    case kCompByte:
    case kCompUnsignedByte:  return 1;
    case kCompShort:
    case kCompUnsignedShort: return 2;
    case kCompUnsignedInt:
    case kCompFloat:         return 4;
    default:                 return 0;
    }
}

int ComponentsPerElement(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2")   return 2;
    if (type == "VEC3")   return 3;
    if (type == "VEC4")   return 4;
    if (type == "MAT4")   return 16;
    return 0;
}

struct BufferView {
    int byteOffset = 0;
    int byteLength = 0;
    int byteStride = 0;    // 0 means tightly packed
};

struct Accessor {
    int bufferView    = -1;
    int byteOffset    = 0;
    int componentType = 0;
    int count         = 0;
    std::string type;
};

// Resolves an accessor to a byte range inside the BIN chunk, verifying that it
// actually fits. glTF lets an accessor claim more than its bufferView holds, and
// trusting it is a straight out-of-bounds read.
bool AccessorRange(const Accessor& a, const std::vector<BufferView>& views,
                   std::size_t binSize, std::size_t& outOffset,
                   std::size_t& outStride, int& outComps) {
    if (a.bufferView < 0 || a.bufferView >= static_cast<int>(views.size())) return false;
    const BufferView& v = views[static_cast<std::size_t>(a.bufferView)];

    outComps = ComponentsPerElement(a.type);
    const int compSize = ComponentByteSize(a.componentType);
    if (outComps == 0 || compSize == 0 || a.count < 0) return false;

    const std::size_t elemSize = static_cast<std::size_t>(outComps) *
                                 static_cast<std::size_t>(compSize);
    outStride = v.byteStride > 0 ? static_cast<std::size_t>(v.byteStride) : elemSize;

    const std::size_t start = static_cast<std::size_t>(v.byteOffset) +
                              static_cast<std::size_t>(a.byteOffset);
    if (a.count == 0) { outOffset = start; return start <= binSize; }

    // Last element's end, which is where a bad count shows up.
    const std::size_t span = outStride * static_cast<std::size_t>(a.count - 1) + elemSize;

    if (start > binSize || span > binSize - start) return false;
    // The accessor must also fit within its own bufferView as well as inside
    // the BIN chunk; that is the case the tests exercise.
    const std::size_t viewEnd = static_cast<std::size_t>(v.byteOffset) +
                                static_cast<std::size_t>(v.byteLength);
    if (start + span > viewEnd) return false;

    outOffset = start;
    return true;
}

// Reads a FLOAT accessor into a flat float array.
bool ReadFloats(const Accessor& a, const std::vector<BufferView>& views,
                const uint8_t* bin, std::size_t binSize, std::vector<float>& out) {
    if (a.componentType != kCompFloat) return false;
    std::size_t off = 0, stride = 0;
    int comps = 0;
    if (!AccessorRange(a, views, binSize, off, stride, comps)) return false;

    out.resize(static_cast<std::size_t>(a.count) * static_cast<std::size_t>(comps));
    for (int i = 0; i < a.count; ++i) {
        const uint8_t* src = bin + off + stride * static_cast<std::size_t>(i);
        for (int c = 0; c < comps; ++c) {
            float f = 0.0f;
            std::memcpy(&f, src + static_cast<std::size_t>(c) * sizeof(float),
                        sizeof(float));
            out[static_cast<std::size_t>(i) * static_cast<std::size_t>(comps) +
                static_cast<std::size_t>(c)] = f;
        }
    }
    return true;
}

// Any scalar component type as float: integers raw (JOINTS_0) or, with
// `normalized`, scaled to 0..1 (an integer WEIGHTS_0 is always normalized).
bool ReadAnyFloats(const Accessor& a, const std::vector<BufferView>& views, const uint8_t* bin,
                   std::size_t binSize, bool normalized, std::vector<float>& out) {
    if (a.componentType == kCompFloat) return ReadFloats(a, views, bin, binSize, out);
    int size = 0;
    float scale = 1.0f;
    if (a.componentType == kCompUnsignedByte) {
        size = 1;
        scale = normalized ? 1.0f / 255.0f : 1.0f;
    } else if (a.componentType == kCompUnsignedShort) {
        size = 2;
        scale = normalized ? 1.0f / 65535.0f : 1.0f;
    } else {
        return false;
    }
    std::size_t off = 0, stride = 0;
    int comps = 0;
    if (!AccessorRange(a, views, binSize, off, stride, comps)) return false;
    out.resize(static_cast<std::size_t>(a.count) * static_cast<std::size_t>(comps));
    for (int i = 0; i < a.count; ++i) {
        const uint8_t* src = bin + off + stride * static_cast<std::size_t>(i);
        for (int c = 0; c < comps; ++c) {
            uint32_t v = src[c * size];
            if (size == 2) v |= static_cast<uint32_t>(src[c * size + 1]) << 8;
            out[static_cast<std::size_t>(i) * static_cast<std::size_t>(comps) + static_cast<std::size_t>(c)] =
                static_cast<float>(v) * scale;
        }
    }
    return true;
}

// Reads an index accessor, downconverting to 16-bit. See glb.h for why this is
// mandatory rather than an optimisation.
GlbError ReadIndices(const Accessor& a, const std::vector<BufferView>& views,
                     const uint8_t* bin, std::size_t binSize, int vertexCount,
                     std::vector<uint16_t>& out) {
    if (a.componentType != kCompUnsignedInt &&
        a.componentType != kCompUnsignedShort &&
        a.componentType != kCompUnsignedByte) {
        return GlbError::UnsupportedComponentType;
    }
    if (vertexCount > kMaxVerticesPerPrimitive) return GlbError::TooManyVertices;

    std::size_t off = 0, stride = 0;
    int comps = 0;
    if (!AccessorRange(a, views, binSize, off, stride, comps)) {
        return GlbError::AccessorOutOfRange;
    }

    out.resize(static_cast<std::size_t>(a.count));
    for (int i = 0; i < a.count; ++i) {
        const uint8_t* src = bin + off + stride * static_cast<std::size_t>(i);
        uint32_t v = 0;
        if (a.componentType == kCompUnsignedInt) {
            v = ReadU32(src);
        } else if (a.componentType == kCompUnsignedShort) {
            v = static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8);
        } else {
            v = src[0];
        }
        // An index past the vertex count would be an out-of-bounds fetch on the
        // GPU. Catch it here, where the file name is still in hand.
        if (v >= static_cast<uint32_t>(vertexCount)) return GlbError::AccessorOutOfRange;
        out[static_cast<std::size_t>(i)] = static_cast<uint16_t>(v);
    }
    return GlbError::None;
}

}  // namespace

const char* GlbErrorText(GlbError e) {
    switch (e) {
    case GlbError::None:                     return "ok";
    case GlbError::FileNotFound:             return "file not found";
    case GlbError::TooSmall:                 return "buffer smaller than a GLB header";
    case GlbError::BadMagic:                 return "not a GLB file (bad magic)";
    case GlbError::BadVersion:               return "unsupported glTF version (need 2)";
    case GlbError::TruncatedChunk:           return "chunk runs past end of buffer";
    case GlbError::MissingJsonChunk:         return "no JSON chunk";
    case GlbError::MissingBinChunk:          return "no BIN chunk";
    case GlbError::MalformedJson:            return "malformed glTF JSON";
    case GlbError::NoMeshes:                 return "document contains no meshes";
    case GlbError::UnsupportedMode:          return "primitive mode is not TRIANGLES";
    case GlbError::MissingPosition:          return "primitive has no POSITION attribute";
    case GlbError::UnsupportedComponentType: return "unsupported accessor component type";
    case GlbError::AccessorOutOfRange:       return "accessor overruns its bufferView";
    case GlbError::TooManyVertices:          return "primitive exceeds 65535 vertices; "
                                                    "GLES2 has no 32-bit index path";
    }
    return "unknown error";
}

GlbError LoadGlbFromMemory(const uint8_t* data, std::size_t size, Model& out) {
    out.meshes.clear();
    out.sourceMeshCount = 0;
    if (!data || size < 12) return GlbError::TooSmall;

    if (ReadU32(data) != kMagicGltf) return GlbError::BadMagic;
    if (ReadU32(data + 4) != 2)      return GlbError::BadVersion;

    // Walk the chunk list. The declared total length is not trusted over the
    // size of the buffer actually handed in.
    const char* jsonBegin = nullptr;
    const char* jsonEnd = nullptr;
    const uint8_t* bin = nullptr;
    std::size_t binSize = 0;

    std::size_t off = 12;
    while (off + 8 <= size) {
        const uint32_t chunkLen = ReadU32(data + off);
        const uint32_t chunkType = ReadU32(data + off + 4);
        const std::size_t payload = off + 8;
        if (chunkLen > size || payload + chunkLen > size) return GlbError::TruncatedChunk;

        if (chunkType == kChunkJson && !jsonBegin) {
            jsonBegin = reinterpret_cast<const char*>(data + payload);
            jsonEnd = jsonBegin + chunkLen;
        } else if (chunkType == kChunkBin && !bin) {
            bin = data + payload;
            binSize = chunkLen;
        }
        off = payload + chunkLen;
    }
    if (!jsonBegin) return GlbError::MissingJsonChunk;
    if (!bin)       return GlbError::MissingBinChunk;

    // --- bufferViews -------------------------------------------------------
    std::vector<BufferView> views;
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (FindArray(jsonBegin, jsonEnd, "bufferViews", &ab, &ae)) {
        std::vector<Span> objs;
        SplitObjects(ab, ae, objs);
        views.reserve(objs.size());
        for (const Span& o : objs) {
            BufferView v{};
            v.byteOffset = ReadInt(o.first, o.second, "byteOffset", 0);
            v.byteLength = ReadInt(o.first, o.second, "byteLength", 0);
            v.byteStride = ReadInt(o.first, o.second, "byteStride", 0);
            views.push_back(v);
        }
    }

    // --- materials ---------------------------------------------------------
    //
    // material -> pbrMetallicRoughness.baseColorTexture -> textures[i].source
    // -> images[j].bufferView. Three indirections, each of which glTF permits
    // to be absent, so every hop is checked. The image bytes are not decoded:
    // they are handed on encoded, and only the offline baker decodes them.
    {
        std::vector<int> textureToImage;
        if (FindArray(jsonBegin, jsonEnd, "textures", &ab, &ae)) {
            std::vector<Span> objs;
            SplitObjects(ab, ae, objs);
            textureToImage.reserve(objs.size());
            for (const Span& o : objs) {
                textureToImage.push_back(ReadInt(o.first, o.second, "source", -1));
            }
        }

        std::vector<int> imageToView;
        if (FindArray(jsonBegin, jsonEnd, "images", &ab, &ae)) {
            std::vector<Span> objs;
            SplitObjects(ab, ae, objs);
            imageToView.reserve(objs.size());
            for (const Span& o : objs) {
                imageToView.push_back(ReadInt(o.first, o.second, "bufferView", -1));
            }
        }

        if (FindArray(jsonBegin, jsonEnd, "materials", &ab, &ae)) {
            std::vector<Span> objs;
            SplitObjects(ab, ae, objs);
            out.materials.reserve(objs.size());
            for (const Span& o : objs) {
                MaterialImage mi;

                const char* pb = nullptr;
                const char* pe = nullptr;
                if (core::json::FindObject(o.first, o.second,
                                           "pbrMetallicRoughness", &pb, &pe)) {
                    const char* cb = nullptr;
                    const char* ce = nullptr;
                    if (FindArray(pb, pe, "baseColorFactor", &cb, &ce)) {
                        std::vector<double> v;
                        core::json::ReadNumberArray(cb, ce, v);
                        for (std::size_t i = 0; i < 4 && i < v.size(); ++i) {
                            mi.baseColour[i] = static_cast<float>(v[i]);
                        }
                    }

                    const char* tb = nullptr;
                    const char* te = nullptr;
                    if (core::json::FindObject(pb, pe, "baseColorTexture",
                                               &tb, &te)) {
                        const int tex = ReadInt(tb, te, "index", -1);
                        if (tex >= 0 && tex < static_cast<int>(textureToImage.size())) {
                            const int img = textureToImage[static_cast<std::size_t>(tex)];
                            if (img >= 0 && img < static_cast<int>(imageToView.size())) {
                                const int vi = imageToView[static_cast<std::size_t>(img)];
                                if (vi >= 0 && vi < static_cast<int>(views.size())) {
                                    const BufferView& v = views[static_cast<std::size_t>(vi)];
                                    const std::size_t off =
                                        static_cast<std::size_t>(v.byteOffset);
                                    const std::size_t len =
                                        static_cast<std::size_t>(v.byteLength);
                                    // A view that claims more than the BIN chunk
                                    // holds is a straight out-of-bounds read.
                                    if (off <= binSize && len <= binSize - off) {
                                        mi.data = bin + off;
                                        mi.size = len;
                                    }
                                }
                            }
                        }
                    }
                }
                out.materials.push_back(mi);
            }
        }
    }

    // --- accessors ---------------------------------------------------------
    std::vector<Accessor> accessors;
    if (FindArray(jsonBegin, jsonEnd, "accessors", &ab, &ae)) {
        std::vector<Span> objs;
        SplitObjects(ab, ae, objs);
        accessors.reserve(objs.size());
        for (const Span& o : objs) {
            Accessor a{};
            a.bufferView    = ReadInt(o.first, o.second, "bufferView", -1);
            a.byteOffset    = ReadInt(o.first, o.second, "byteOffset", 0);
            a.componentType = ReadInt(o.first, o.second, "componentType", 0);
            a.count         = ReadInt(o.first, o.second, "count", 0);
            a.type          = ReadString(o.first, o.second, "type");
            accessors.push_back(a);
        }
    }

    auto accessorAt = [&accessors](int i) -> const Accessor* {
        if (i < 0 || i >= static_cast<int>(accessors.size())) return nullptr;
        return &accessors[static_cast<std::size_t>(i)];
    };

    // --- meshes ------------------------------------------------------------
    const char* mb = nullptr;
    const char* me = nullptr;
    if (!FindArray(jsonBegin, jsonEnd, "meshes", &mb, &me)) return GlbError::NoMeshes;

    std::vector<Span> meshObjs;
    SplitObjects(mb, me, meshObjs);
    if (meshObjs.empty()) return GlbError::NoMeshes;
    out.sourceMeshCount = static_cast<int>(meshObjs.size());

    for (std::size_t meshOrdinal = 0; meshOrdinal < meshObjs.size(); ++meshOrdinal) {
        const Span& mo = meshObjs[meshOrdinal];
        const std::string meshName = ReadString(mo.first, mo.second, "name");

        const char* pb = nullptr;
        const char* pe = nullptr;
        if (!FindArray(mo.first, mo.second, "primitives", &pb, &pe)) continue;

        std::vector<Span> prims;
        SplitObjects(pb, pe, prims);
        for (const Span& po : prims) {
            // Default mode in glTF is 4 (TRIANGLES) when absent.
            const int mode = ReadInt(po.first, po.second, "mode", kModeTriangles);
            if (mode != kModeTriangles) { out.meshes.clear(); return GlbError::UnsupportedMode; }

            const char* atb = nullptr;
            const char* ate = nullptr;
            if (!FindObject(po.first, po.second, "attributes", &atb, &ate)) {
                out.meshes.clear();
                return GlbError::MissingPosition;
            }

            const int posIdx = ReadInt(atb, ate, "POSITION", -1);
            const Accessor* posA = accessorAt(posIdx);
            if (!posA) { out.meshes.clear(); return GlbError::MissingPosition; }

            Mesh mesh;
            mesh.name = meshName;
            mesh.materialIndex = ReadInt(po.first, po.second, "material", -1);
            mesh.sourceMeshIndex = static_cast<int>(meshOrdinal);

            if (!ReadFloats(*posA, views, bin, binSize, mesh.positions)) {
                out.meshes.clear();
                return posA->componentType == kCompFloat ? GlbError::AccessorOutOfRange
                                                         : GlbError::UnsupportedComponentType;
            }
            const int vertexCount = mesh.VertexCount();
            if (vertexCount > kMaxVerticesPerPrimitive) {
                out.meshes.clear();
                return GlbError::TooManyVertices;
            }

            const int nrmIdx = ReadInt(atb, ate, "NORMAL", -1);
            if (const Accessor* nrmA = accessorAt(nrmIdx)) {
                if (!ReadFloats(*nrmA, views, bin, binSize, mesh.normals)) {
                    out.meshes.clear();
                    return GlbError::AccessorOutOfRange;
                }
            }
            // Normals are relied on by the light bake, so synthesise a neutral
            // set rather than leaving the array a different length to positions.
            if (mesh.normals.size() != mesh.positions.size()) {
                mesh.normals.assign(mesh.positions.size(), 0.0f);
                for (std::size_t i = 2; i < mesh.normals.size(); i += 3) {
                    mesh.normals[i] = 1.0f;
                }
            }

            const int tanIdx = ReadInt(atb, ate, "TANGENT", -1);
            if (const Accessor* tanA = accessorAt(tanIdx)) {
                if (!ReadFloats(*tanA, views, bin, binSize, mesh.tangents)) {
                    out.meshes.clear();
                    return GlbError::AccessorOutOfRange;
                }
                if (mesh.tangents.size() != static_cast<std::size_t>(vertexCount) * 4) mesh.tangents.clear();
            }

            const int uvIdx = ReadInt(atb, ate, "TEXCOORD_0", -1);
            if (const Accessor* uvA = accessorAt(uvIdx)) {
                if (!ReadFloats(*uvA, views, bin, binSize, mesh.uvs)) {
                    out.meshes.clear();
                    return GlbError::AccessorOutOfRange;
                }
            }

            // The skin's joints and weights, 4 per vertex.
            const Accessor* jA = accessorAt(ReadInt(atb, ate, "JOINTS_0", -1));
            const Accessor* wA = accessorAt(ReadInt(atb, ate, "WEIGHTS_0", -1));
            if (jA && wA) {
                std::vector<float> j;
                if (ReadAnyFloats(*jA, views, bin, binSize, false, j) &&
                    ReadAnyFloats(*wA, views, bin, binSize, true, mesh.weights) &&
                    j.size() == static_cast<std::size_t>(vertexCount) * 4 && mesh.weights.size() == j.size()) {
                    mesh.joints.resize(j.size());
                    for (std::size_t k = 0; k < j.size(); ++k) mesh.joints[k] = static_cast<uint8_t>(j[k]);
                } else {
                    mesh.weights.clear();
                }
            }

            const int idxIdx = ReadInt(po.first, po.second, "indices", -1);
            if (const Accessor* idxA = accessorAt(idxIdx)) {
                const GlbError e = ReadIndices(*idxA, views, bin, binSize, vertexCount,
                                               mesh.indices);
                if (e != GlbError::None) { out.meshes.clear(); return e; }
            } else {
                // Non-indexed primitive: synthesise a sequential index buffer so
                // the renderer only ever has one path to maintain.
                mesh.indices.resize(static_cast<std::size_t>(vertexCount));
                for (int i = 0; i < vertexCount; ++i) {
                    mesh.indices[static_cast<std::size_t>(i)] = static_cast<uint16_t>(i);
                }
            }

            out.meshes.push_back(std::move(mesh));
        }
    }

    if (out.meshes.empty()) return GlbError::NoMeshes;
    return GlbError::None;
}

GlbError LoadGlb(const char* path, Model& out) {
    out.meshes.clear();
    out.sourceMeshCount = 0;
    if (!path) return GlbError::FileNotFound;

    std::FILE* f = std::fopen(path, "rb");
    if (!f) return GlbError::FileNotFound;

    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return GlbError::TooSmall; }

    std::vector<uint8_t> buf(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    buf.resize(got);

    const GlbError e = LoadGlbFromMemory(buf.data(), buf.size(), out);
    if (e == GlbError::None) out.name = path;
    return e;
}

}  // namespace assets
