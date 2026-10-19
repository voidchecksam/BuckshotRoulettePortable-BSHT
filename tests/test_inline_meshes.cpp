// The fallback for meshes with no name.
//
// Nodes authored directly in main.tscn rather than imported appear in no
// .glb. The test that matters asserts every mesh node ends up in exactly one
// bucket, so a hole in the room cannot appear unnoticed.
#include "test_data.h"

#include <gtest/gtest.h>

#include "assets/animated_nodes.h"
#include "assets/inline_meshes.h"
#include "assets/mesh_index.h"
#include "assets/room.h"

#include <string>

using namespace assets;

namespace {
std::string Extracted(const char* rel) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + rel;
}
}  // namespace

TEST(InlineMeshes, ParsesSurfaces) {
    InlineMeshLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"meshes":[
      {"path":"a/plane","class":"PlaneMesh","surfaces":[
        {"positions":[0,0,0, 1,0,0, 0,1,0],
         "normals":[0,1,0, 0,1,0, 0,1,0],
         "uvs":[0,0, 1,0, 0,1],
         "indices":[0,1,2]}]}]})"));

    ASSERT_EQ(lib.Size(), 1u);
    const InlineMesh* m = lib.Find("a/plane");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->className, "PlaneMesh");
    ASSERT_EQ(m->surfaces.size(), 1u);
    EXPECT_EQ(m->surfaces[0].VertexCount(), 3);
    EXPECT_EQ(m->surfaces[0].TriangleCount(), 1);
    EXPECT_EQ(lib.Find("nope"), nullptr);
}

TEST(InlineMeshes, SynthesisesNormalsRatherThanLeavingThemShort) {
    // The light bake indexes normals alongside positions; a short array would
    // read out of bounds.
    InlineMeshLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"meshes":[
      {"path":"a/b","class":"BoxMesh","surfaces":[
        {"positions":[0,0,0, 1,0,0, 0,1,0],"normals":[],"uvs":[],
         "indices":[0,1,2]}]}]})"));
    const InlineMesh* m = lib.Find("a/b");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->surfaces[0].normals.size(), m->surfaces[0].positions.size());
}

TEST(InlineMeshes, RejectsMalformedInput) {
    InlineMeshLibrary lib;
    EXPECT_FALSE(lib.LoadFromString(nullptr));
    EXPECT_FALSE(lib.LoadFromString("{}"));
    EXPECT_FALSE(lib.LoadFromString(R"({"meshes":[]})"));
    EXPECT_FALSE(lib.LoadFromFile("nope.json"));
}

// The health counter's lightning icons: an embedded albedo texture whose alpha
// is the icon, on an alpha-transparent material that emits green
// (main.tscn sub_resource 234). Dropping any of the three drew orange squares.
TEST(InlineMeshes, ParsesTheSurfaceMaterial) {
    InlineMeshLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"meshes":[
      {"path":"a/symbol","class":"PlaneMesh","surfaces":[
        {"positions":[0,0,0, 1,0,0, 0,1,0],"normals":[],"uvs":[],
         "indices":[0,1,2],
         "material":{"albedo":[1,1,1,1],"texture":"res://scenes/main.tscn::233",
                     "texture_file":"inline_textures/defib.png",
                     "transparent":true,"unshaded":false,"emission":[0,1,0]}}]},
      {"path":"a/plain","class":"BoxMesh","surfaces":[
        {"positions":[0,0,0, 1,0,0, 0,1,0],"normals":[],"uvs":[],
         "indices":[0,1,2]}]}]})"));

    const InlineMesh* m = lib.Find("a/symbol");
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->materials.size(), m->surfaces.size());
    const InlineMaterial& mat = m->materials[0];
    EXPECT_EQ(mat.textureFile, "inline_textures/defib.png");
    EXPECT_TRUE(mat.transparent);
    EXPECT_FLOAT_EQ(mat.emission[0], 0.0f);
    EXPECT_FLOAT_EQ(mat.emission[1], 1.0f);
    EXPECT_FLOAT_EQ(mat.emission[2], 0.0f);

    // No material object: an untextured, opaque, non-emissive surface.
    const InlineMesh* plain = lib.Find("a/plain");
    ASSERT_NE(plain, nullptr);
    ASSERT_EQ(plain->materials.size(), 1u);
    EXPECT_TRUE(plain->materials[0].textureFile.empty());
    EXPECT_FALSE(plain->materials[0].transparent);
    EXPECT_FLOAT_EQ(plain->materials[0].emission[1], 0.0f);
}

TEST(InlineMeshes, ATransparentTexturedSurfaceBecomesATexturedPropWithoutBakedEmission) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    const std::string symbol = "tabletop parent/main tabletop/health counter/"
                               "health counter ui parent/health UI_dealer side/symbol_dealer1";
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    InlineMeshLibrary inl;
    const std::string json = R"({"meshes":[{"path":")" + symbol + R"(","class":"PlaneMesh",
      "surfaces":[{"positions":[0,0,0, 1,0,0, 0,1,0],"normals":[0,1,0, 0,1,0, 0,1,0],
        "uvs":[0,0, 1,0, 0,1],"indices":[0,1,2],
        "material":{"albedo":[1,1,1,1],"texture":"res://scenes/main.tscn::233",
                    "texture_file":"inline_textures/defib.png",
                    "transparent":true,"unshaded":false,"emission":[0,1,0]}}]}]})";
    ASSERT_TRUE(inl.LoadFromString(json.c_str()));

    RoomBuildOptions opts;
    opts.modelDir = Extracted("models");
    opts.inlineMeshes = &inl;
    opts.keepHiddenAsProps = true;

    Room room;
    ASSERT_TRUE(room.Build(scene, index, opts));

    const RoomProp* prop = nullptr;
    for (const RoomProp& p : room.Props()) {
        if (p.nodePath == symbol) prop = &p;
    }
    ASSERT_NE(prop, nullptr) << "the icon is not a prop";
    // Keyed by its texture file, which the texture bake decodes.
    EXPECT_EQ(prop->sourceGlb, "inline_textures/defib.png");
    EXPECT_EQ(prop->materialIndex, 0);
    // Godot adds emission on top of lit albedo, and for a PROP the renderer
    // adds it live from the node's material (the loader reads it from the
    // export). Baked into the vertex colour as well, it was counted twice, so
    // the vertex colour is the light alone: the same bake with no emission.
    ASSERT_EQ(prop->mesh.colours.size(), 12u);
    std::string dark = json;
    dark.replace(dark.find(R"("emission":[0,1,0])"), 18, R"("emission":[0,0,0])");
    InlineMeshLibrary inlDark;
    ASSERT_TRUE(inlDark.LoadFromString(dark.c_str()));
    opts.inlineMeshes = &inlDark;
    Room roomDark;
    ASSERT_TRUE(roomDark.Build(scene, index, opts));
    const RoomProp* darkProp = nullptr;
    for (const RoomProp& p : roomDark.Props()) {
        if (p.nodePath == symbol) darkProp = &p;
    }
    ASSERT_NE(darkProp, nullptr);
    EXPECT_EQ(prop->mesh.colours, darkProp->mesh.colours);
}

TEST(InlineMeshes, LoadsTheRealExport) {
    BSHT_NEEDS_FILE(Extracted("inline_meshes.json"));
    InlineMeshLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(Extracted("inline_meshes.json").c_str()))
        << "run tools/godot/export_inline_meshes.gd first";

    // The documented gap, exactly: 76 nodes.
    EXPECT_EQ(lib.Stats().nodes, 76);
    EXPECT_EQ(lib.Stats().surfaces, 76);
    EXPECT_EQ(lib.Stats().triangles, 2018);

    for (const InlineMesh& m : lib.Meshes()) {
        EXPECT_FALSE(m.nodePath.empty());
        for (const Mesh& s : m.surfaces) {
            EXPECT_GT(s.VertexCount(), 0);
            EXPECT_EQ(s.indices.size() % 3, 0u);
            EXPECT_LE(s.VertexCount(), 65535) << "GLES2 has no 32-bit indices";
            for (uint16_t i : s.indices) {
                ASSERT_LT(i, s.VertexCount()) << m.nodePath;
            }
        }
    }
}

TEST(InlineMeshes, TheRoomDrawsThemAndEveryNodeIsAccountedFor) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    InlineMeshLibrary inl;
    ASSERT_TRUE(inl.LoadFromFile(Extracted("inline_meshes.json").c_str()));

    RoomBuildOptions opts;
    opts.modelDir = Extracted("models");
    opts.filter.radius = 60.0f;
    opts.inlineMeshes = &inl;

    Room room;
    ASSERT_TRUE(room.Build(scene, index, opts));
    const RoomStats& s = room.Stats();

    EXPECT_EQ(s.inlinePrimitives, 76);
    EXPECT_GT(s.inlineDrawn, 0) << "the fallback supplied nothing";
    EXPECT_LE(s.inlineDrawn, s.inlinePrimitives);

    // The invariant: every mesh node lands in exactly one bucket. If this sum
    // drifts, geometry is going missing without anything reporting it.
    EXPECT_EQ(s.nodesConsidered,
              s.nodesDrawn + s.outsideFilter + s.invisible + s.unresolved)
        << "a node fell out of the accounting";
}

TEST(InlineMeshes, WithoutTheFallbackTheSameNodesAreCountedButNotDrawn) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    // Proves the fallback is what closes the gap, rather than the gap having
    // quietly closed itself some other way.
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    InlineMeshLibrary inl;
    ASSERT_TRUE(inl.LoadFromFile(Extracted("inline_meshes.json").c_str()));

    RoomBuildOptions without;
    without.modelDir = Extracted("models");
    without.filter.radius = 60.0f;

    RoomBuildOptions with = without;
    with.inlineMeshes = &inl;

    Room a, b;
    ASSERT_TRUE(a.Build(scene, index, without));
    ASSERT_TRUE(b.Build(scene, index, with));

    EXPECT_EQ(a.Stats().inlineDrawn, 0);
    EXPECT_GT(b.Stats().inlineDrawn, 0);
    EXPECT_GT(b.Stats().triangles, a.Stats().triangles)
        << "the fallback added no geometry";
    EXPECT_GT(b.Stats().nodesDrawn, a.Stats().nodesDrawn);
}
