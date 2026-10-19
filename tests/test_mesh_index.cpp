// The scene-node to glTF-mesh join.
//
// The end-to-end test is the one that matters: it takes real scene nodes,
// resolves them through the real table, loads the real .glb, and checks the
// resolved geometry exists. That is the chain the room is assembled from, and
// a broken link anywhere in it shows here.
#include "test_data.h"

#include <gtest/gtest.h>

#include "assets/glb.h"
#include "assets/mesh_index.h"
#include "assets/scene.h"

#include <string>

using namespace assets;

namespace {

std::string Extracted(const char* rel) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + rel;
}

}  // namespace

TEST(MeshIndex, ResolvesANameToItsFileAndMeshIndex) {
    MeshIndex idx;
    ASSERT_TRUE(idx.LoadFromString(
        R"({"models":[{"glb":"a.glb","meshes":["one","two"]},
                     {"glb":"b.glb","meshes":["three"]}]})"));
    EXPECT_EQ(idx.ModelCount(), 2u);

    const MeshRef r = idx.Resolve("two");
    ASSERT_TRUE(r.Valid());
    EXPECT_EQ(r.glb, "a.glb");
    EXPECT_EQ(r.meshIndex, 1);

    const MeshRef r2 = idx.Resolve("three");
    ASSERT_TRUE(r2.Valid());
    EXPECT_EQ(r2.glb, "b.glb");
    EXPECT_EQ(r2.meshIndex, 0);
}

TEST(MeshIndex, EmptyEntriesKeepLaterIndicesAligned) {
    // A mesh that failed to join is stored as "". Dropping it instead would
    // shift every following name onto the wrong mesh: the room would assemble
    // from real geometry in the wrong places, with nothing reporting an error.
    MeshIndex idx;
    ASSERT_TRUE(idx.LoadFromString(
        R"({"models":[{"glb":"a.glb","meshes":["", "", "third"]}]})"));
    const MeshRef r = idx.Resolve("third");
    ASSERT_TRUE(r.Valid());
    EXPECT_EQ(r.meshIndex, 2) << "empty entries did not hold their slots";
}

TEST(MeshIndex, UnknownNameResolvesToAnInvalidRef) {
    MeshIndex idx;
    ASSERT_TRUE(idx.LoadFromString(
        R"({"models":[{"glb":"a.glb","meshes":["one"]}]})"));
    EXPECT_FALSE(idx.Resolve("nope").Valid());
    EXPECT_FALSE(idx.Resolve("").Valid());
}

TEST(MeshIndex, HandlesNamesContainingSpaces) {
    // Every real name has spaces: "backroom blockout6_Cylinder_020".
    MeshIndex idx;
    ASSERT_TRUE(idx.LoadFromString(
        R"({"models":[{"glb":"a.glb","meshes":["backroom blockout6_Cylinder_020"]}]})"));
    EXPECT_TRUE(idx.Resolve("backroom blockout6_Cylinder_020").Valid());
}

TEST(MeshIndex, RejectsMalformedInput) {
    MeshIndex idx;
    EXPECT_FALSE(idx.LoadFromString(nullptr));
    EXPECT_FALSE(idx.LoadFromString("{}"));
    EXPECT_FALSE(idx.LoadFromString("{\"models\":[]}"));
    EXPECT_FALSE(idx.LoadFromFile(nullptr));
    EXPECT_FALSE(idx.LoadFromFile("nope.json"));
}

TEST(Glb, RecordsWhichGltfMeshEachPrimitiveCameFrom) {
    BSHT_NEEDS_FILE(Extracted("models/backroom_blockout6.glb"));
    // Model::meshes is per-PRIMITIVE; the join table is keyed by glTF MESH
    // index. Without sourceMeshIndex the two cannot be related, and a lookup
    // would land on whatever primitive happens to sit at that position.
    Model m;
    const std::string glb = Extracted("models/backroom_blockout6.glb");
    ASSERT_EQ(LoadGlb(glb.c_str(), m), GlbError::None);

    ASSERT_GT(m.meshes.size(), 0u);
    EXPECT_GT(m.meshes.size(), static_cast<std::size_t>(m.sourceMeshCount))
        << "this model should have more primitives than glTF meshes";

    for (const Mesh& mesh : m.meshes) {
        EXPECT_GE(mesh.sourceMeshIndex, 0);
        EXPECT_LT(mesh.sourceMeshIndex, m.sourceMeshCount);
    }
    // Primitives of one glTF mesh are contiguous and in order.
    int prev = -1;
    for (const Mesh& mesh : m.meshes) {
        EXPECT_GE(mesh.sourceMeshIndex, prev);
        prev = mesh.sourceMeshIndex;
    }
}

TEST(MeshIndex, ResolvesRealSceneNodesToRealGeometry) {
    BSHT_NEEDS_FILE(Extracted("models/_mesh_names.json"));
    MeshIndex idx;
    ASSERT_TRUE(idx.LoadFromFile(Extracted("models/_mesh_names.json").c_str()))
        << "run tools/godot/export_scene_meshes.gd first";
    EXPECT_EQ(idx.ModelCount(), 55u);
    EXPECT_EQ(idx.Size(), 544u);

    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));

    int resolved = 0, inlinePrims = 0, unresolved = 0;
    for (const SceneNode& n : scene.Nodes()) {
        if (!n.HasMesh()) continue;
        if (n.IsInlinePrimitive()) { ++inlinePrims; continue; }
        if (idx.Resolve(n.meshId).Valid()) ++resolved;
        else ++unresolved;
    }
    EXPECT_EQ(resolved, 582);
    EXPECT_EQ(inlinePrims, 76);
    EXPECT_EQ(unresolved, 0) << "a mesh the scene draws is missing from the library";

    // Now prove a resolved reference actually points at loadable geometry.
    int checked = 0;
    for (const SceneNode& n : scene.Nodes()) {
        if (!n.HasMesh() || n.IsInlinePrimitive()) continue;
        const MeshRef ref = idx.Resolve(n.meshId);
        if (!ref.Valid()) continue;

        Model model;
        const std::string path = Extracted(("models/" + ref.glb).c_str());
        ASSERT_EQ(LoadGlb(path.c_str(), model), GlbError::None) << ref.glb;
        ASSERT_LT(ref.meshIndex, model.sourceMeshCount)
            << n.meshId << " -> " << ref.glb << " mesh " << ref.meshIndex;

        int primitives = 0;
        for (const Mesh& mesh : model.meshes) {
            if (mesh.sourceMeshIndex == ref.meshIndex) ++primitives;
        }
        EXPECT_GT(primitives, 0)
            << n.meshId << " resolved to a mesh index with no primitives";

        if (++checked >= 25) break;   // 25 real round trips is enough evidence
    }
    EXPECT_EQ(checked, 25);
}
