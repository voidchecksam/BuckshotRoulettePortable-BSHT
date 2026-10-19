// GLB loader.
//
// Malformed cases are driven from in-memory buffers built by MakeGlb() below,
// so each failure mode is a few lines rather than a broken file checked into the
// repo. The real extracted models are then loaded and cross-checked against
// _manifest.json, whose counts Godot wrote, so agreeing with them is real
// evidence rather than the loader agreeing with itself.
#include "test_data.h"

#include <gtest/gtest.h>

#include "assets/glb.h"
#include "core/json_lite.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace assets;

namespace {

std::string ModelsDir() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/models/";
}

void PushU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

// Builds a GLB container around the given JSON and BIN payloads.
std::vector<uint8_t> MakeGlb(const std::string& json,
                             const std::vector<uint8_t>& bin,
                             uint32_t magic = 0x46546C67u,
                             uint32_t version = 2) {
    std::string j = json;
    while (j.size() % 4) j.push_back(' ');          // chunks are 4-byte aligned
    std::vector<uint8_t> bn = bin;
    while (bn.size() % 4) bn.push_back(0);

    std::vector<uint8_t> out;
    PushU32(out, magic);
    PushU32(out, version);
    PushU32(out, 0);                                 // total length, patched below
    PushU32(out, static_cast<uint32_t>(j.size()));
    PushU32(out, 0x4E4F534Au);                       // 'JSON'
    out.insert(out.end(), j.begin(), j.end());
    if (!bn.empty()) {
        PushU32(out, static_cast<uint32_t>(bn.size()));
        PushU32(out, 0x004E4942u);                   // 'BIN'
        out.insert(out.end(), bn.begin(), bn.end());
    }
    const uint32_t total = static_cast<uint32_t>(out.size());
    out[8]  = static_cast<uint8_t>(total & 0xFF);
    out[9]  = static_cast<uint8_t>((total >> 8) & 0xFF);
    out[10] = static_cast<uint8_t>((total >> 16) & 0xFF);
    out[11] = static_cast<uint8_t>((total >> 24) & 0xFF);
    return out;
}

// One triangle: 3 vertices, POSITION + NORMAL, 3 UNSIGNED_INT indices.
// Layout: [36 bytes positions][36 bytes normals][12 bytes indices]
std::vector<uint8_t> TriangleBin() {
    std::vector<uint8_t> bin;
    const float pos[9] = {0,0,0,  1,0,0,  0,1,0};
    const float nrm[9] = {0,0,1,  0,0,1,  0,0,1};
    const uint32_t idx[3] = {0,1,2};
    auto append = [&bin](const void* p, std::size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        bin.insert(bin.end(), b, b + n);
    };
    append(pos, sizeof(pos));
    append(nrm, sizeof(nrm));
    append(idx, sizeof(idx));
    return bin;
}

// componentType 5125 = UNSIGNED_INT, 5126 = FLOAT.
std::string TriangleJson(int mode = 4, int indexComponentType = 5125,
                         int positionCount = 3) {
    char buf[1400];
    std::snprintf(buf, sizeof(buf), R"({
 "asset":{"version":"2.0"},
 "buffers":[{"byteLength":84}],
 "bufferViews":[
   {"buffer":0,"byteOffset":0,"byteLength":36},
   {"buffer":0,"byteOffset":36,"byteLength":36},
   {"buffer":0,"byteOffset":72,"byteLength":12}],
 "accessors":[
   {"bufferView":0,"componentType":5126,"count":%d,"type":"VEC3"},
   {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
   {"bufferView":2,"componentType":%d,"count":3,"type":"SCALAR"}],
 "meshes":[{"name":"tri","primitives":[
   {"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0,"mode":%d}]}]
})", positionCount, indexComponentType, mode);
    return std::string(buf);
}

GlbError LoadBuf(const std::vector<uint8_t>& b, Model& m) {
    return LoadGlbFromMemory(b.data(), b.size(), m);
}

}  // namespace

// ------------------------------------------------------------ container errors

TEST(Glb, RejectsBadMagic) {
    Model m;
    auto b = MakeGlb(TriangleJson(), TriangleBin(), /*magic=*/0xDEADBEEF);
    EXPECT_EQ(LoadBuf(b, m), GlbError::BadMagic);
    EXPECT_TRUE(m.meshes.empty());
}

TEST(Glb, RejectsWrongVersion) {
    Model m;
    auto b = MakeGlb(TriangleJson(), TriangleBin(), 0x46546C67u, /*version=*/1);
    EXPECT_EQ(LoadBuf(b, m), GlbError::BadVersion);
}

TEST(Glb, RejectsBufferSmallerThanHeader) {
    Model m;
    std::vector<uint8_t> tiny{1, 2, 3, 4};
    EXPECT_EQ(LoadBuf(tiny, m), GlbError::TooSmall);
}

TEST(Glb, RejectsChunkRunningPastEndOfBuffer) {
    Model m;
    auto b = MakeGlb(TriangleJson(), TriangleBin());
    b.resize(b.size() - 20);          // truncate mid-BIN
    EXPECT_EQ(LoadBuf(b, m), GlbError::TruncatedChunk);
}

TEST(Glb, RejectsMissingBinChunk) {
    Model m;
    auto b = MakeGlb(TriangleJson(), {});
    EXPECT_EQ(LoadBuf(b, m), GlbError::MissingBinChunk);
}

TEST(Glb, ReportsAMissingFileRatherThanCrashing) {
    Model m;
    EXPECT_EQ(LoadGlb("assets/extracted/models/does_not_exist.glb", m),
              GlbError::FileNotFound);
    EXPECT_EQ(LoadGlb(nullptr, m), GlbError::FileNotFound);
}

// -------------------------------------------------------------- schema errors

TEST(Glb, RejectsNonTriangleMode) {
    // mode 5 is TRIANGLE_STRIP. Silently skipping it would leave a hole in the
    // room with no clue as to why.
    Model m;
    auto b = MakeGlb(TriangleJson(/*mode=*/5), TriangleBin());
    EXPECT_EQ(LoadBuf(b, m), GlbError::UnsupportedMode);
}

TEST(Glb, RejectsUnsupportedIndexComponentType) {
    Model m;
    auto b = MakeGlb(TriangleJson(4, /*indexComponentType=*/5126), TriangleBin());
    EXPECT_EQ(LoadBuf(b, m), GlbError::UnsupportedComponentType);
}

TEST(Glb, RejectsAccessorOverrunningItsBufferView) {
    // 9 vec3s claimed from a 36-byte view that holds 3.
    Model m;
    auto b = MakeGlb(TriangleJson(4, 5125, /*positionCount=*/9), TriangleBin());
    EXPECT_EQ(LoadBuf(b, m), GlbError::AccessorOutOfRange);
}

TEST(Glb, RejectsDocumentWithNoMeshes) {
    Model m;
    auto b = MakeGlb(R"({"asset":{"version":"2.0"},"meshes":[]})", TriangleBin());
    EXPECT_EQ(LoadBuf(b, m), GlbError::NoMeshes);
}

// -------------------------------------------------------------- happy path

TEST(Glb, LoadsASingleTriangle) {
    Model m;
    auto b = MakeGlb(TriangleJson(), TriangleBin());
    ASSERT_EQ(LoadBuf(b, m), GlbError::None) << GlbErrorText(LoadBuf(b, m));
    ASSERT_EQ(m.meshes.size(), 1u);
    const Mesh& mesh = m.meshes[0];
    EXPECT_EQ(mesh.VertexCount(), 3);
    EXPECT_EQ(mesh.TriangleCount(), 1);
    EXPECT_EQ(mesh.materialIndex, 0);
    EXPECT_FLOAT_EQ(mesh.positions[3], 1.0f);   // second vertex x
    EXPECT_FLOAT_EQ(mesh.normals[2], 1.0f);     // first vertex normal z
    EXPECT_EQ(mesh.indices[0], 0);
    EXPECT_EQ(mesh.indices[2], 2);
}

TEST(Glb, DownconvertsThirtyTwoBitIndicesToSixteen) {
    // GLES2 has no 32-bit index support and the Vita does not report
    // GL_OES_element_index_uint, so this conversion is not an optimisation --
    // it is the difference between running and not running on hardware.
    Model m;
    auto b = MakeGlb(TriangleJson(4, /*indexComponentType=*/5125), TriangleBin());
    ASSERT_EQ(LoadBuf(b, m), GlbError::None);
    EXPECT_EQ(sizeof(m.meshes[0].indices[0]), 2u);
    EXPECT_EQ(m.meshes[0].indices.size(), 3u);
}

// ------------------------------------------------- the real extracted models

TEST(Glb, LoadsEveryExtractedModelAndMatchesTheManifest) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/models/_manifest.json");
    // _manifest.json counts come from Godot (export_scene_meshes.gd), so the
    // loader is measured against outside evidence, not against itself.
    const std::string manifestPath =
        std::string(BSHT_SOURCE_DIR) + "/assets/extracted/models/_manifest.json";
    std::FILE* f = std::fopen(manifestPath.c_str(), "rb");
    ASSERT_NE(f, nullptr) << "run tools/godot/export_scene_meshes.gd first: " << manifestPath;
    std::string doc;
    {
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        doc.resize(static_cast<std::size_t>(n));
        const std::size_t got = std::fread(&doc[0], 1, static_cast<std::size_t>(n), f);
        std::fclose(f);
        doc.resize(got);
    }

    const char* begin = doc.data();
    const char* end = doc.data() + doc.size();
    const char* mb = nullptr;
    const char* me = nullptr;
    ASSERT_TRUE(core::json::FindArray(begin, end, "models", &mb, &me))
        << "manifest has no \"models\" array";

    std::vector<core::json::Span> entries;
    core::json::SplitObjects(mb, me, entries);
    ASSERT_EQ(entries.size(), 55u) << "expected 55 extracted models";

    int filesChecked = 0;
    long long totalTris = 0;
    long long totalSourceMeshes = 0;
    long long totalPrimitives = 0;
    for (const auto& e : entries) {
        const std::string file = core::json::ReadString(e.first, e.second, "glb");
        const int expectMeshes = core::json::ReadInt(e.first, e.second, "meshes", -1);
        const int expectTris   = core::json::ReadInt(e.first, e.second, "triangles", -1);
        ASSERT_FALSE(file.empty());

        Model m;
        const GlbError err = LoadGlb((ModelsDir() + file).c_str(), m);
        ASSERT_EQ(err, GlbError::None) << file << ": " << GlbErrorText(err);

        // The manifest counts glTF meshes; m.meshes is per-primitive. Compare
        // like with like.
        if (expectMeshes >= 0) {
            EXPECT_EQ(m.sourceMeshCount, expectMeshes) << file;
        }
        if (expectTris >= 0) {
            EXPECT_EQ(m.TotalTriangles(), expectTris) << file;
        }

        // Every mesh must be internally consistent, whatever the manifest says.
        for (const Mesh& mesh : m.meshes) {
            EXPECT_GT(mesh.VertexCount(), 0) << file;
            EXPECT_EQ(mesh.indices.size() % 3, 0u) << file;
            EXPECT_EQ(mesh.normals.size(), mesh.positions.size()) << file;
            if (mesh.HasUvs()) {
                EXPECT_EQ(mesh.uvs.size() / 2, mesh.positions.size() / 3) << file;
            }
            for (uint16_t i : mesh.indices) {
                ASSERT_LT(i, mesh.VertexCount()) << file << ": index out of range";
            }
        }
        totalTris += m.TotalTriangles();
        totalSourceMeshes += m.sourceMeshCount;
        totalPrimitives += static_cast<long long>(m.meshes.size());
        ++filesChecked;
    }

    EXPECT_EQ(filesChecked, 55);
    // The meshes the v2.2 scenes draw: 544 meshes, 488,031 triangles, 775 primitives.
    EXPECT_EQ(totalSourceMeshes, 544) << "glTF mesh count drifted";
    EXPECT_EQ(totalTris, 488031) << "total triangle count drifted";
    EXPECT_EQ(totalPrimitives, 775) << "primitive count drifted";
}

// The normal maps: Godot perturbs the normal with the mesh tangent, so the
// bake needs TANGENT (xyz + handedness w) for every vertex. Every extracted
// primitive carries one.
TEST(Glb, LoadsTangentsWithTheirHandedness) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/models/health_counter_main1.glb");
    const std::string path =
        std::string(BSHT_SOURCE_DIR) + "/assets/extracted/models/health_counter_main1.glb";
    Model model;
    ASSERT_EQ(LoadGlb(path.c_str(), model), GlbError::None);
    ASSERT_FALSE(model.meshes.empty());
    for (const Mesh& m : model.meshes) {
        ASSERT_EQ(m.tangents.size(), static_cast<std::size_t>(m.VertexCount()) * 4);
        for (std::size_t v = 0; v < m.tangents.size(); v += 4) {
            EXPECT_TRUE(m.tangents[v + 3] == 1.0f || m.tangents[v + 3] == -1.0f);
        }
    }
}
