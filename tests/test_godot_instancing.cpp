// PackedScene::instantiate, Node::add_child and Node::queue_free: what
// ShellSpawner.SpawnShells does with instances/shell.tscn. Engine rules from
// Godot 4.1.4-stable:
//   - add_child with a name a sibling already has renames the child to
//     "@<Class>@<n>" (node.cpp:1253, _validate_child_name);
//   - the added subtree readies children first (_propagate_ready, node.cpp:217);
//   - queue_free'd nodes are deleted in _flush_delete_queue, after the process
//     pass and before timers (scene_tree.cpp:516-518), so they still process
//     in the frame they were queued, and a timer that fires this frame sees
//     them gone.
#include "godot/packed_scene.h"

#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kShell = R"({"format":4,"scene":"instances/shell.tscn","root_name":"shell parent","nodes":[
 {"path":".","parent":-1,"class":"MeshInstance3D","script":"","visible":true,
  "xform":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"rotation":[0,0,0],"scale":[1,1,1],
  "layers":1,"transparency":0,"mesh":"shotgun shells export1_Cylinder_022",
  "materials":[{"albedo":[1,1,1,1],"transparency":0},{"albedo":[1,0.282353,0.298039,1],"transparency":0}]},
 {"path":"shell branch","parent":0,"class":"Node","script":"ShellBranch","exports":{"isLive":false,"mesh":{"node":"."}}},
 {"path":"shell cap","parent":0,"class":"MeshInstance3D","script":"","visible":true,
  "xform":[1,0,0,0,0,1,0,0,0,0,1,0,0,0.574879,0,1],"rotation":[0,0,0],"scale":[1,1,1],
  "layers":1,"transparency":0,"mesh":"cap","materials":[]}]})";

struct Table {
    godot::SceneTree tree;
    godot::Node3D* spawn = nullptr;
    godot::Node3D* after = nullptr;
    const godot::PackedScene* shell = nullptr;

    Table() {
        godot::Node* root = tree.add_node(std::make_unique<godot::Node>("main"), nullptr);
        spawn = godot::cast_to<godot::Node3D>(tree.add_node(std::make_unique<godot::Node3D>("compartment"), root));
        after = godot::cast_to<godot::Node3D>(tree.add_node(std::make_unique<godot::Node3D>("after"), root));
        shell = godot::PackedScene::load_from_string("res://instances/shell.tscn", kShell);
    }
};

}  // namespace

TEST(GodotInstancing, InstantiateBuildsADetachedCopyWithItsChildren) {
    Table t;
    ASSERT_NE(t.shell, nullptr);
    EXPECT_EQ(godot::PackedScene::find("res://instances/shell.tscn"), t.shell);

    std::unique_ptr<godot::Node> inst = t.shell->instantiate();
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->get_name(), "shell parent");
    EXPECT_EQ(inst->get_tree(), nullptr);
    ASSERT_EQ(inst->get_child_count(), 2);
    EXPECT_EQ(inst->get_child(0)->get_name(), "shell branch");
    EXPECT_EQ(inst->get_child(1)->get_parent(), inst.get());
    auto* body = godot::cast_to<godot::MeshInstance3D>(inst.get());
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->get_template_key(), "instances/shell.tscn:shell parent");
    EXPECT_EQ(godot::cast_to<godot::MeshInstance3D>(inst->get_child(1))->get_template_key(),
              "instances/shell.tscn:shell parent/shell cap");
    ASSERT_EQ(body->surface_count(), 2);
    EXPECT_NEAR(body->get_surface_material(1).albedo.g, 0.282353f, 1e-6f);
}

TEST(GodotInstancing, AddChildAdoptsTheSubtreeInTreeOrder) {
    Table t;
    std::unique_ptr<godot::Node> inst = t.shell->instantiate();
    godot::Node* raw = inst.get();

    EXPECT_EQ(t.spawn->add_child(std::move(inst)), raw);

    EXPECT_EQ(raw->get_parent(), t.spawn);
    EXPECT_EQ(raw->get_tree(), &t.tree);
    EXPECT_EQ(raw->get_child(1)->get_tree(), &t.tree);
    EXPECT_EQ(raw->get_path(), "compartment/shell parent");
    EXPECT_EQ(t.tree.get_node("compartment/shell parent/shell cap"), raw->get_child(1));
    const std::vector<godot::Node*>& order = t.tree.nodes_in_tree_order();
    ASSERT_EQ(order.size(), 6u);
    EXPECT_EQ(order[1], t.spawn);
    EXPECT_EQ(order[2], raw);
    EXPECT_EQ(order[3], raw->get_child(0));
    EXPECT_EQ(order[4], raw->get_child(1));
    EXPECT_EQ(order[5], t.after);
    EXPECT_EQ(t.tree.template_instances().size(), 2u);
}

TEST(GodotInstancing, ASecondInstanceWithTheSameNameGetsAGeneratedOne) {
    Table t;
    godot::Node* first = t.spawn->add_child(t.shell->instantiate());
    godot::Node* second = t.spawn->add_child(t.shell->instantiate());

    EXPECT_EQ(first->get_name(), "shell parent");
    EXPECT_EQ(second->get_name().rfind("@MeshInstance3D@", 0), 0u) << second->get_name();
    EXPECT_NE(first->get_path(), second->get_path());
    EXPECT_EQ(t.tree.get_node(second->get_path() + "/shell cap"), second->get_child(1));
    // The second shell's subtree follows the first's.
    const std::vector<godot::Node*>& order = t.tree.nodes_in_tree_order();
    EXPECT_EQ(order[5], second);
    EXPECT_EQ(t.tree.template_instances().size(), 4u);
}

TEST(GodotInstancing, QueueFreeDeletesAfterTheProcessPassAndBeforeTimers) {
    Table t;
    godot::Node* shell = t.spawn->add_child(t.shell->instantiate());
    const std::string path = shell->get_path();

    shell->queue_free();
    EXPECT_EQ(t.tree.get_node(path), shell);   // still there until the frame flushes

    t.tree.process_frame(1.0 / 60.0);

    EXPECT_EQ(t.tree.get_node(path), nullptr);
    EXPECT_EQ(t.tree.get_node(path + "/shell cap"), nullptr);
    EXPECT_EQ(t.spawn->get_child_count(), 0);
    EXPECT_EQ(t.tree.nodes_in_tree_order().size(), 3u);
    EXPECT_TRUE(t.tree.template_instances().empty());
}

TEST(GodotInstancing, QueueFreeTwiceAndFreeingAChildWithItsParentAreSafe) {
    Table t;
    godot::Node* shell = t.spawn->add_child(t.shell->instantiate());
    shell->get_child(1)->queue_free();
    shell->queue_free();
    shell->queue_free();
    t.tree.process_frame(1.0 / 60.0);
    EXPECT_EQ(t.spawn->get_child_count(), 0);
    EXPECT_EQ(t.tree.nodes_in_tree_order().size(), 3u);
}

TEST(GodotInstancing, ALoadedSceneIsFoundByItsResourcePathOnly) {
    EXPECT_EQ(godot::PackedScene::find("res://instances/no such.tscn"), nullptr);
    EXPECT_EQ(godot::PackedScene::load("res://instances/other.tscn", "no/such/file.json"), nullptr);
}
