// Template props: geometry baked once for a PackedScene's meshes and drawn for
// every clone instantiated at runtime, plus the surface index every prop
// records so a per-surface override material tints the right primitive
// (ShellClass.ApplyStatus recolours surface 1 only).
#include "assets/mesh_index.h"
#include "assets/room.h"
#include "assets/room_file.h"
#include "assets/scene.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace assets;

namespace {

RoomProp Triangle(const std::string& nodePath, const std::string& templateKey, int surface) {
    RoomProp p;
    p.nodePath = nodePath;
    p.templateKey = templateKey;
    p.surface = surface;
    p.node = -1;
    p.materialIndex = 2;
    p.mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    p.mesh.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    p.mesh.uvs = {0, 0, 1, 0, 0, 1};
    p.mesh.colours = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255};
    p.mesh.indices = {0, 1, 2};
    p.boundsMin[0] = p.boundsMin[1] = p.boundsMin[2] = 0.0f;
    p.boundsMax[0] = p.boundsMax[1] = 1.0f;
    p.boundsMax[2] = 0.0f;
    return p;
}

long FileSize(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fclose(f);
    return n;
}

constexpr const char* kShellBody = "shotgun shells export1_Cylinder_022";
constexpr const char* kShellCap = "shotgun shells export1_Cylinder_045";

}  // namespace

TEST(RoomTemplates, TemplatePropsRoundTripTheirKeyAndEveryPropItsSurface) {
    Room room;
    std::vector<RoomProp> props;
    props.push_back(Triangle("", "instances/shell.tscn:shell parent", 1));
    props.push_back(Triangle("tabletop parent/prop", "", 3));
    room.AdoptProps(std::move(props));
    const std::string path = std::string(BSHT_BINARY_DIR) + "/templates.room";

    ASSERT_TRUE(WriteRoomFile(path.c_str(), room));
    Room back;
    ASSERT_EQ(ReadRoomFile(path.c_str(), back), RoomFileError::None);

    ASSERT_EQ(back.Props().size(), 2u);
    EXPECT_TRUE(back.Props()[0].IsTemplate());
    EXPECT_EQ(back.Props()[0].templateKey, "instances/shell.tscn:shell parent");
    EXPECT_EQ(back.Props()[0].surface, 1);
    EXPECT_FALSE(back.Props()[1].IsTemplate());
    EXPECT_EQ(back.Props()[1].nodePath, "tabletop parent/prop");
    EXPECT_EQ(back.Props()[1].surface, 3);
    EXPECT_EQ(static_cast<long>(MeasureRoom(room).bytes), FileSize(path));
}

TEST(RoomTemplates, AddTemplatePropsBakesOnePropPerPrimitiveOfTheShellMeshes) {
    const std::string root = std::string(BSHT_SOURCE_DIR) + "/assets/extracted/";
    Scene scene;
    MeshIndex index;
    if (!scene.LoadFromFile((root + "scene_graph.json").c_str()) ||
        !index.LoadFromFile((root + "models/_mesh_names.json").c_str())) {
        GTEST_SKIP() << "extracted scene graph or mesh index missing";
    }
    RoomBuildOptions opts;
    opts.modelDir = root + "models";
    Room room;

    // instances/shell.tscn: the body has two surfaces (brass, then the
    // live-red sheet ApplyStatus overrides); the cap is its own mesh.
    const int body = room.AddTemplateProps("instances/shell.tscn:shell parent", kShellBody, index,
                                           core::Mat4{}, scene, opts);
    const int cap = room.AddTemplateProps("instances/shell.tscn:shell parent/shell cap", kShellCap,
                                          index, core::Mat4{}, scene, opts);

    EXPECT_EQ(body, 2);
    EXPECT_GE(cap, 1);
    ASSERT_EQ(static_cast<int>(room.Props().size()), body + cap);
    EXPECT_EQ(room.Props()[0].surface, 0);
    EXPECT_EQ(room.Props()[1].surface, 1);
    EXPECT_EQ(room.Props()[0].templateKey, "instances/shell.tscn:shell parent");
    EXPECT_EQ(room.Props()[static_cast<std::size_t>(body)].templateKey,
              "instances/shell.tscn:shell parent/shell cap");
    for (const RoomProp& p : room.Props()) {
        EXPECT_TRUE(p.IsTemplate());
        EXPECT_EQ(p.node, -1);
        EXPECT_TRUE(p.nodePath.empty());
        EXPECT_FALSE(p.HasBounds());
        EXPECT_GT(p.mesh.TriangleCount(), 0);
    }
    EXPECT_EQ(room.Stats().props, body + cap);
    EXPECT_EQ(room.AddTemplateProps("k", "no such mesh", index, core::Mat4{}, scene, opts), 0);
}
