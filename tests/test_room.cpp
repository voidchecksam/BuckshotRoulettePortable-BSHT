// Room assembly: scene + index + models -> merged, lit, world-space batches.
//
// The merge rules are pinned with small synthetic inputs, then the real room is
// built end to end, asserting the batch count is lower than the primitive
// count (otherwise "merging" is just copying).
#include "test_data.h"

#include <gtest/gtest.h>

#include "assets/room.h"

#include <cmath>
#include <string>

using namespace assets;

namespace {

std::string Extracted(const char* rel) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + rel;
}

RoomBuildOptions RealOptions() {
    RoomBuildOptions o;
    o.modelDir = Extracted("models");
    return o;
}

}  // namespace

TEST(Room, BuildsTheRealRoomFromTheRealScene) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));

    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));

    Room room;
    ASSERT_TRUE(room.Build(scene, index, RealOptions()));

    const RoomStats& s = room.Stats();
    // Every mesh-bearing node is accounted for in exactly one bucket. If this
    // sum drifts, geometry is going missing without anything reporting it.
    EXPECT_EQ(s.nodesConsidered,
              s.nodesDrawn + s.inlinePrimitives + s.unresolved + s.invisible);
    EXPECT_EQ(s.nodesConsidered, 658);
    EXPECT_EQ(s.inlinePrimitives, 76);

    EXPECT_GT(s.nodesDrawn, 0);
    EXPECT_GT(s.triangles, 0);
    EXPECT_GT(s.batches, 0);

    // Merging must actually reduce work, or it is pointless.
    EXPECT_LT(s.batches, s.primitivesMerged)
        << "batches did not reduce below primitive count; merge is a no-op";

    // Batch integrity: 16-bit indices, in range, whole triangles, lit.
    for (const RoomBatch& b : room.Batches()) {
        const Mesh& m = b.mesh;
        EXPECT_LE(m.VertexCount(), 65535)
            << "batch exceeds the 16-bit index limit; unindexable on GLES2";
        EXPECT_EQ(m.indices.size() % 3, 0u);
        EXPECT_EQ(m.normals.size(), m.positions.size());
        EXPECT_EQ(m.colours.size(), static_cast<std::size_t>(m.VertexCount()) * 4);
        EXPECT_EQ(m.uvs.size(), static_cast<std::size_t>(m.VertexCount()) * 2)
            << "UVs fell out of step with vertices in " << b.sourceGlb;
        for (uint16_t i : m.indices) {
            ASSERT_LT(i, m.VertexCount()) << "index out of range in " << b.sourceGlb;
        }
    }
}

TEST(Room, GeometryLandsInWorldSpaceNotModelSpace) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    // A room whose geometry all sits near the origin means the node transforms
    // were never applied: everything would be stacked in one place.
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));

    Room room;
    ASSERT_TRUE(room.Build(scene, index, RealOptions()));

    float lo = 1e30f, hi = -1e30f;
    for (const RoomBatch& b : room.Batches()) {
        for (std::size_t i = 0; i < b.mesh.positions.size(); i += 3) {
            lo = std::min(lo, b.mesh.positions[i]);
            hi = std::max(hi, b.mesh.positions[i]);
        }
    }
    EXPECT_GT(hi - lo, 10.0f) << "room has no spatial extent; transforms not applied";
}

TEST(Room, SkipsInvisibleNodesWhenAsked) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));

    RoomBuildOptions withHidden = RealOptions();
    withHidden.skipInvisible = false;
    Room all;
    ASSERT_TRUE(all.Build(scene, index, withHidden));

    Room visibleOnly;
    ASSERT_TRUE(visibleOnly.Build(scene, index, RealOptions()));

    // Every node must land in exactly one bucket, in both builds: nothing is
    // silently dropped.
    //
    // A node rejected for being hidden would not necessarily have been drawn
    // otherwise: it can just as well be outside the area filter or unresolved
    // (those checks run after the visibility one), so the buckets are summed,
    // not compared to the drawn count.
    const RoomStats& v = visibleOnly.Stats();
    const RoomStats& a = all.Stats();
    EXPECT_EQ(v.nodesConsidered, v.Accounted())
        << "a node went missing from the accounting";
    EXPECT_EQ(a.nodesConsidered, a.Accounted())
        << "a node went missing from the accounting";

    EXPECT_GT(v.invisible, 0) << "nothing was hidden; the filter did nothing";
    EXPECT_EQ(0, a.invisible) << "skipInvisible=false still hid something";
    EXPECT_LE(v.nodesDrawn, a.nodesDrawn);
    EXPECT_LE(v.triangles, a.triangles);
}

TEST(Room, RespectsTheSixteenBitVertexCap) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    // Forcing a small cap must produce more, smaller batches rather than one
    // oversized buffer that cannot be indexed on hardware.
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));

    RoomBuildOptions tight = RealOptions();
    tight.maxVerticesPerBatch = 4096;
    Room room;
    ASSERT_TRUE(room.Build(scene, index, tight));

    for (const RoomBatch& b : room.Batches()) {
        EXPECT_LE(b.mesh.VertexCount(), 4096 + 65535)
            << "a single primitive larger than the cap must still be allowed";
    }

    Room normal;
    ASSERT_TRUE(normal.Build(scene, index, RealOptions()));
    EXPECT_GT(room.Stats().batches, normal.Stats().batches)
        << "a tighter cap did not produce more batches";
    // Same geometry either way; only the packing changed.
    EXPECT_EQ(room.Stats().triangles, normal.Stats().triangles);
}

TEST(Room, EmptySceneProducesNothingRatherThanCrashing) {
    Scene scene;
    ASSERT_TRUE(scene.LoadFromString(
        "{\"nodes\":[{\"name\":\"r\",\"path\":\".\",\"type\":\"Node3D\"}]}"));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromString(
        R"({"models":[{"glb":"a.glb","meshes":["x"]}]})"));

    Room room;
    EXPECT_FALSE(room.Build(scene, index, RealOptions()));
    EXPECT_EQ(room.Stats().nodesDrawn, 0);
}

TEST(Room, ReportsUnresolvedNodesRatherThanDroppingThemSilently) {
    // A node whose mesh_id is in no table at all must be COUNTED. A quietly
    // dropped node is a hole in the room with nothing pointing at the cause.
    Scene scene;
    ASSERT_TRUE(scene.LoadFromString(
        "{\"nodes\":["
        "{\"name\":\"r\",\"path\":\".\",\"type\":\"Node3D\"},"
        "{\"name\":\"m\",\"path\":\"m\",\"type\":\"MeshInstance3D\","
        "\"mesh_id\":\"nothing known\",\"transform\":"
        "[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]}]}"));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromString(
        R"({"models":[{"glb":"a.glb","meshes":["x"]}]})"));

    Room room;
    EXPECT_FALSE(room.Build(scene, index, RealOptions()));
    EXPECT_EQ(room.Stats().nodesConsidered, 1);
    EXPECT_EQ(room.Stats().unresolved, 1);
}

TEST(Room, BatchesCarryWorldSpaceTangentsForTheNormalMaps) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    // The bake perturbs normals with the tangent frame. A batch is merged in
    // world space, so its tangents must turn with its normals: a tangent left
    // in model space stops being perpendicular to its normal on any rotated
    // node. Geometry without tangents is padded with zeros.
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    Room room;
    ASSERT_TRUE(room.Build(scene, index, RealOptions()));
    // Some source tangents are not perpendicular to begin with, so the bar is
    // 80%, not all. Model-space tangents on rotated nodes pull it far below.
    long checked = 0, perpendicular = 0;
    for (const RoomBatch& b : room.Batches()) {
        const Mesh& m = b.mesh;
        ASSERT_EQ(m.tangents.size(), static_cast<std::size_t>(m.VertexCount()) * 4) << b.sourceGlb;
        for (int v = 0; v < m.VertexCount(); ++v) {
            const float* t = &m.tangents[static_cast<std::size_t>(v) * 4];
            if (t[3] == 0.0f) continue;
            const float* n = &m.normals[static_cast<std::size_t>(v) * 3];
            if (std::fabs(t[0] * n[0] + t[1] * n[1] + t[2] * n[2]) < 0.05f) ++perpendicular;
            ++checked;
        }
    }
    EXPECT_GT(checked, 100000);
    EXPECT_GT(static_cast<double>(perpendicular) / static_cast<double>(checked), 0.8);
}
