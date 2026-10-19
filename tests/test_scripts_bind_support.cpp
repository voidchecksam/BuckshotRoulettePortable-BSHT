// scripts::ExportReader and scripts::Binder on hand-written exports: every value
// shape export_runtime_scene.gd writes, including nulls inside arrays, nested
// resources whose keys shadow the node's own, and paths that must resolve to the
// right C++ type or be reported.
#include "scripts/bind_support.h"

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kExports = R"({
 "amount": 3, "dur": 0.6, "flag": true,
 "name": "A \"q\"\nB",
 "at": {"v3": [1, 2.5, -3]},
 "shell": {"res": "res://instances/shell.tscn"},
 "click": {"res": "res://audio/dialogue click4.ogg"},
 "clicks": [{"res": "res://audio/a.ogg"}, {"res": "res://audio/b c.ogg"}],
 "lines": ["ONE\nTWO", "", "THREE"],
 "spots": [{"v3": [0, 0, 0]}, {"v3": [1, 1, 1]}],
 "known": [true, false, true],
 "mat": {"res": "res://m.tres", "material": {"albedo": [0.3, 0.5, 1, 1], "transparency": 0}},
 "cam": {"node": "Camera"},
 "missing": null,
 "players": [{"node": "Camera/ap"}, null, {"node": "Camera"}],
 "socket": {"resource": "CameraSocket", "fields": {"socketName": "home", "fov": 65.5, "cam": {"node": "nope"}}},
 "sockets": [{"resource": "CameraSocket", "fields": {"socketName": "a"}}, null]
})";

}  // namespace

TEST(ScriptsExportReader, ReadsScalarsStringsAndVectors) {
    const scripts::ExportReader ex(kExports);
    EXPECT_EQ(ex.integer("amount", 0), 3);
    EXPECT_DOUBLE_EQ(ex.number("dur", 0.0), 0.6);
    EXPECT_TRUE(ex.boolean("flag", false));
    EXPECT_EQ(ex.string("name"), "A \"q\"\nB");
    const godot::Vector3 at = ex.vector3("at", {});
    EXPECT_FLOAT_EQ(at.y, 2.5f);
    EXPECT_FLOAT_EQ(at.z, -3.0f);
    EXPECT_EQ(ex.integer("absent", 7), 7);
}

TEST(ScriptsExportReader, ResourcesBecomeFileNamesAndMaterials) {
    const scripts::ExportReader ex(kExports);
    EXPECT_EQ(ex.res_name("click"), "dialogue click4.ogg");
    EXPECT_EQ(ex.res_path("shell"), "res://instances/shell.tscn");
    EXPECT_EQ(ex.res_names("clicks"), (std::vector<std::string>{"a.ogg", "b c.ogg"}));
    // The full paths too (the blood splatter's texture array keys by path).
    const std::vector<std::string> paths = ex.res_paths("clicks");
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0].substr(paths[0].size() - 5), "a.ogg");
    EXPECT_EQ(paths[0].rfind("res://", 0), 0u);
    const godot::SurfaceMaterial m = ex.material("mat");
    EXPECT_FLOAT_EQ(m.albedo.r, 0.3f);
    EXPECT_FLOAT_EQ(m.albedo_at_load.b, 1.0f);
}

TEST(ScriptsExportReader, ArraysKeepTheirPositions) {
    const scripts::ExportReader ex(kExports);
    EXPECT_EQ(ex.strings("lines"), (std::vector<std::string>{"ONE\nTWO", "", "THREE"}));
    ASSERT_EQ(ex.vector3s("spots").size(), 2u);
    EXPECT_FLOAT_EQ(ex.vector3s("spots")[1].x, 1.0f);
    EXPECT_EQ(ex.booleans("known"), (std::vector<bool>{true, false, true}));
    EXPECT_EQ(ex.node_paths("players"), (std::vector<std::string>{"Camera/ap", "", "Camera"}));
}

TEST(ScriptsExportReader, NodePathsAndResourceFieldsReadOnlyTheirOwnLevel) {
    const scripts::ExportReader ex(kExports);
    EXPECT_EQ(ex.node_path("cam"), "Camera");
    EXPECT_EQ(ex.node_path("missing"), "");
    const scripts::ExportReader socket = ex.resource_fields("socket");
    ASSERT_FALSE(socket.empty());
    EXPECT_EQ(socket.string("socketName"), "home");
    EXPECT_TRUE(ex.resource_fields("missing").empty());
    ASSERT_EQ(ex.resource_array("sockets").size(), 2u);
    EXPECT_TRUE(ex.resource_array("sockets")[1].empty());
}

TEST(ScriptsBinder, ResolvesNodesByTypeAndReportsMismatches) {
    godot::SceneTree tree;
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("main"), nullptr);
    godot::Node* cam = tree.add_node(std::make_unique<godot::Camera3D>("Camera"), root);
    tree.add_node(std::make_unique<godot::AnimationPlayer>("ap"), cam);

    std::vector<std::string> errors;
    scripts::Binder b(root, "Test", &errors);
    const scripts::ExportReader ex(kExports);

    godot::Camera3D* c = nullptr;
    b.node(ex, "cam", c);
    EXPECT_EQ(c, cam);

    std::vector<godot::AnimationPlayer*> players;
    b.nodes(ex, "players", players);
    ASSERT_EQ(players.size(), 3u);
    EXPECT_NE(players[0], nullptr);
    EXPECT_EQ(players[1], nullptr);
    EXPECT_EQ(players[2], nullptr);   // a Camera3D is not an AnimationPlayer

    godot::Label* none = nullptr;
    b.node(ex, "missing", none);
    EXPECT_EQ(none, nullptr);

    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("Test.players"), std::string::npos);
    EXPECT_NE(errors[0].find("Camera"), std::string::npos);
}

TEST(ScriptsBinder, ResolvesPathsRelativeToAnInstanceRoot) {
    godot::Node detached("shell parent");
    std::vector<std::string> errors;
    scripts::Binder b(&detached, "ShellClass", &errors);
    EXPECT_EQ(b.resolve("."), &detached);
    EXPECT_EQ(b.resolve("nope"), nullptr);
}
