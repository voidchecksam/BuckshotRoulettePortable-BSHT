// The engine rules the scripts rely on, each pinned by a case that failed in
// the old port: rotation is radians, visibility follows the DIRECT Node3D
// parent, render layers are a bitmask, and a sheared authored basis survives
// until something writes rotation or scale.
#include "godot/control.h"
#include "godot/node.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

godot::Node3D* Add3D(godot::SceneTree& t, const char* name, godot::Node* parent) {
    return godot::cast_to<godot::Node3D>(
        t.add_node(std::make_unique<godot::Node3D>(name), parent));
}

}  // namespace

// ShellClass.ApplyStatus: set_surface_override_material(1, mat) recolours the
// shell body and nothing else.
TEST(GodotNode, SurfaceOverrideTintsOnlyThatSurface) {
    godot::MeshInstance3D m("m");
    m.add_surface_material({{1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 0});
    m.add_surface_material({{1.0f, 0.28f, 0.3f, 1.0f}, {1.0f, 0.28f, 0.3f, 1.0f}, 0});

    m.set_surface_override_material(1, {{0.3f, 0.51f, 1.0f, 1.0f}, {0.3f, 0.51f, 1.0f, 1.0f}, 0});

    const godot::Color t0 = m.get_surface_tint_since_load(0);
    const godot::Color t1 = m.get_surface_tint_since_load(1);
    EXPECT_FLOAT_EQ(t0.r, 1.0f);
    EXPECT_FLOAT_EQ(t0.b, 1.0f);
    EXPECT_NEAR(t1.r, 0.3f, 1e-5f);
    EXPECT_NEAR(t1.b, 1.0f / 0.3f, 1e-4f);
    EXPECT_NEAR(m.get_active_surface_albedo(1).b, 1.0f, 1e-6f);
    // Out of range is ignored, as Godot errors and does nothing.
    m.set_surface_override_material(7, {});
    EXPECT_FLOAT_EQ(m.get_surface_tint_since_load(7).r, 1.0f);
}

// Label::set_visible_characters (label.cpp:899): ratio = amount / total,
// total = xl_text.length() (label.cpp:984), newlines included.
TEST(GodotNode, LabelVisibleRatioFollowsVisibleCharacters) {
    godot::Label l("t");
    l.set_text("AB\nC");
    l.set_visible_characters(2);
    EXPECT_EQ(l.get_total_character_count(), 4);
    EXPECT_DOUBLE_EQ(l.get_visible_ratio(), 0.5);
    l.set_visible_characters(4);
    EXPECT_DOUBLE_EQ(l.get_visible_ratio(), 1.0);
    l.set_text("");
    l.set_visible_characters(1);
    EXPECT_DOUBLE_EQ(l.get_visible_ratio(), 1.0);
}

// CanvasItem::is_visible_in_tree: the chain stops at a parent that is not a
// CanvasItem (the port models CanvasItems as Control).
TEST(GodotNode, ControlVisibleInTreeStopsAtANonControlParent) {
    godot::SceneTree t;
    godot::Node3D* cam = Add3D(t, "Camera", nullptr);
    cam->set_visible(false);
    auto* ui = godot::cast_to<godot::Control>(t.add_node(std::make_unique<godot::Control>("ui"), cam));
    auto* label = godot::cast_to<godot::Label>(t.add_node(std::make_unique<godot::Label>("text"), ui));
    EXPECT_TRUE(label->is_visible_in_tree());
    ui->set_visible(false);
    EXPECT_FALSE(label->is_visible_in_tree());
    ui->set_visible(true);
    // control.cpp grab_focus: a Control is FOCUS_NONE by default (control.h:184)
    // and grabbing focus then does nothing.
    ui->grab_focus();
    EXPECT_FALSE(ui->has_focus());
    ui->set_focus_mode(godot::Control::FOCUS_ALL);
    ui->grab_focus();
    EXPECT_TRUE(ui->has_focus());
    EXPECT_FALSE(label->has_focus());
}

TEST(GodotNode, RotationIsRadiansAndDegreesIsAView) {
    godot::SceneTree t;
    godot::Node3D* n = Add3D(t, "n", nullptr);
    n->set_rotation({0.0f, 3.14159265f, 0.0f});
    EXPECT_NEAR(n->get_rotation_degrees().y, 180.0f, 1e-3f);
    // A 180-degree yaw flips X and Z of the basis.
    EXPECT_NEAR(n->get_transform().m[0], -1.0f, 1e-5f);
    EXPECT_NEAR(n->get_transform().m[10], -1.0f, 1e-5f);
    n->set_rotation_degrees({90.0f, 0.0f, 0.0f});
    EXPECT_NEAR(n->get_rotation().x, 1.5707963f, 1e-5f);
}

TEST(GodotNode, GlobalTransformComposesDownTheChainAndTracksChanges) {
    godot::SceneTree t;
    godot::Node3D* root = Add3D(t, "root", nullptr);
    godot::Node3D* a = Add3D(t, "a", root);
    godot::Node3D* b = Add3D(t, "b", a);
    a->set_position({10.0f, 0.0f, 0.0f});
    b->set_position({0.0f, 1.0f, 0.0f});
    EXPECT_FLOAT_EQ(b->get_global_transform().m[12], 10.0f);
    EXPECT_FLOAT_EQ(b->get_global_transform().m[13], 1.0f);
    a->set_position({-5.0f, 0.0f, 0.0f});   // must invalidate the cached child
    EXPECT_FLOAT_EQ(b->get_global_transform().m[12], -5.0f);
}

TEST(GodotNode, VisibleInTreeAndsTheNode3DParentChain) {
    godot::SceneTree t;
    godot::Node3D* root = Add3D(t, "root", nullptr);
    godot::Node3D* parent = Add3D(t, "parent", root);
    godot::Node3D* child = Add3D(t, "child", parent);
    EXPECT_TRUE(child->is_visible_in_tree());
    parent->set_visible(false);
    EXPECT_TRUE(child->is_visible());
    EXPECT_FALSE(child->is_visible_in_tree());
}

// Godot's data.parent is the direct parent cast to Node3D; a plain Node in
// between starts a new chain for both visibility and transform.
TEST(GodotNode, APlainNodeParentBreaksTheSpatialChain) {
    godot::SceneTree t;
    godot::Node3D* top = Add3D(t, "top", nullptr);
    godot::Node* plain = t.add_node(std::make_unique<godot::Node>("plain"), top);
    godot::Node3D* under = Add3D(t, "under", plain);
    top->set_visible(false);
    top->set_position({100.0f, 0.0f, 0.0f});
    EXPECT_TRUE(under->is_visible_in_tree());
    EXPECT_FLOAT_EQ(under->get_global_transform().m[12], 0.0f);
}

TEST(GodotNode, LayerMaskValuesAreOneBasedBits) {
    godot::SceneTree t;
    auto* m = godot::cast_to<godot::MeshInstance3D>(
        t.add_node(std::make_unique<godot::MeshInstance3D>("m"), nullptr));
    EXPECT_EQ(m->get_layers(), 1u);
    m->set_layers(0u);
    EXPECT_FALSE(m->get_layer_mask_value(1));
    m->set_layer_mask_value(1, true);        // DealerIntelligence.gd:417
    EXPECT_EQ(m->get_layers(), 1u);
    m->set_layer_mask_value(12, true);
    EXPECT_EQ(m->get_layers(), 1u | (1u << 11));
    m->set_layer_mask_value(1, false);
    EXPECT_EQ(m->get_layers(), 1u << 11);
}

TEST(GodotNode, AShearedRestBasisSurvivesAPositionWrite) {
    godot::SceneTree t;
    godot::Node3D* n = Add3D(t, "n", nullptr);
    core::Mat4 sheared;
    sheared.m[4] = 0.3f;   // column 1 leans into X: no TRS form exists
    n->set_rest(sheared, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    n->set_position({1.0f, 2.0f, 3.0f});
    EXPECT_FLOAT_EQ(n->get_transform().m[4], 0.3f);
    EXPECT_FLOAT_EQ(n->get_transform().m[13], 2.0f);
    n->set_rotation({0.0f, 0.0f, 0.0f});     // an explicit write recomposes
    EXPECT_FLOAT_EQ(n->get_transform().m[4], 0.0f);
    EXPECT_FLOAT_EQ(n->get_transform().m[13], 2.0f);
}

TEST(GodotNode, CastToRespectsTheClassHierarchy) {
    godot::SceneTree t;
    godot::Node* label = t.add_node(std::make_unique<godot::Label3D>("l"), nullptr);
    EXPECT_NE(godot::cast_to<godot::GeometryInstance3D>(label), nullptr);
    EXPECT_NE(godot::cast_to<godot::Node3D>(label), nullptr);
    EXPECT_EQ(godot::cast_to<godot::MeshInstance3D>(label), nullptr);
    EXPECT_EQ(godot::cast_to<godot::Camera3D>(label), nullptr);
}

// Camera3D::get_camera_transform (camera_3d.cpp:166-171): the offsets move the
// camera's origin along its own up (column 1) and right (column 0) axes. The
// node's own global transform is left alone: CameraShaker shakes the view,
// not the node.
TEST(GodotCamera3D, OffsetsMoveTheCameraTransformAlongItsOwnAxes) {
    godot::SceneTree t;
    auto* cam = static_cast<godot::Camera3D*>(
        t.add_node(std::make_unique<godot::Camera3D>("Camera"), nullptr));
    cam->set_position({10.0f, 5.0f, 2.0f});

    cam->h_offset = 0.25f;
    cam->v_offset = -0.5f;
    const core::Mat4 view = cam->get_camera_transform();

    // Identity basis: right is +x, up is +y.
    EXPECT_NEAR(view.m[12], 10.25f, 1e-5f);
    EXPECT_NEAR(view.m[13], 4.5f, 1e-5f);
    EXPECT_NEAR(view.m[14], 2.0f, 1e-5f);

    // The node itself has not moved.
    const core::Mat4& g = cam->get_global_transform();
    EXPECT_NEAR(g.m[12], 10.0f, 1e-5f);
    EXPECT_NEAR(g.m[13], 5.0f, 1e-5f);
}

// With no offsets it must be the global transform unchanged, or every existing
// raycast and render path shifts.
TEST(GodotCamera3D, NoOffsetsIsTheGlobalTransform) {
    godot::SceneTree t;
    auto* cam = static_cast<godot::Camera3D*>(
        t.add_node(std::make_unique<godot::Camera3D>("Camera"), nullptr));
    cam->set_position({1.0f, 2.0f, 3.0f});
    cam->set_rotation_degrees({15.0f, 30.0f, 0.0f});

    const core::Mat4 view = cam->get_camera_transform();
    const core::Mat4& g = cam->get_global_transform();
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(view.m[i], g.m[i], 1e-5f) << "element " << i;
}

// ------------------------------------------------ re-parenting (items)
// node.cpp:1429 Node::remove_child and node_3d.cpp:304 Node3D::
// set_global_transform, 4.1.4-stable. ItemManager.SetupItemClear moves each item
// under the grid parent that the "clear items" animation sweeps off the table,
// keeping it exactly where it was: remove_child, add_child, then restore the
// saved global transform.
//
// In Godot remove_child does not free; the caller keeps the node. Here the
// SceneTree owns every node, so remove_child unlinks and ownership stays put,
// and add_child(Node*) re-attaches a node the tree already owns.

TEST(GodotNode, RemoveChildUnlinksWithoutFreeing) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    godot::Node3D* a = Add3D(t, "a", root);
    godot::Node3D* item = Add3D(t, "item", a);
    ASSERT_EQ(t.get_node("a/item"), item);

    a->remove_child(item);
    EXPECT_EQ(a->get_child_count(), 0);
    EXPECT_EQ(item->get_parent(), nullptr);
    EXPECT_EQ(t.get_node("a/item"), nullptr);   // its old path no longer resolves
    item->set_position({1.0f, 2.0f, 3.0f});      // still alive: not freed
    EXPECT_FLOAT_EQ(item->get_position().y, 2.0f);
}

TEST(GodotNode, ARemovedNodeCanBeAddedUnderANewParent) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    godot::Node3D* a = Add3D(t, "a", root);
    godot::Node3D* b = Add3D(t, "b", root);
    godot::Node3D* item = Add3D(t, "item", a);
    godot::Node3D* blade = Add3D(t, "blade", item);   // the subtree moves with it

    a->remove_child(item);
    b->add_child(item);
    EXPECT_EQ(item->get_parent(), b);
    EXPECT_EQ(b->get_child_count(), 1);
    EXPECT_EQ(t.get_node("b/item"), item);
    EXPECT_EQ(t.get_node("b/item/blade"), blade);
    EXPECT_EQ(t.get_node("a/item"), nullptr);
}

TEST(GodotNode, SetGlobalTransformKeepsTheWorldPoseUnderANewParent) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    godot::Node3D* a = Add3D(t, "a", root);
    godot::Node3D* b = Add3D(t, "b", root);
    a->set_position({1.0f, 2.0f, 3.0f});
    b->set_position({-4.0f, 0.0f, 5.0f});
    b->set_rotation_degrees({0.0f, 90.0f, 0.0f});
    godot::Node3D* item = Add3D(t, "item", a);
    item->set_position({0.5f, 0.0f, 0.0f});

    // SetupItemClear, ItemManager.gd:510-513.
    const core::Mat4 orig = item->get_global_transform();
    item->get_parent()->remove_child(item);
    b->add_child(item);
    item->set_global_transform(orig);

    const core::Mat4& now = item->get_global_transform();
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(now.m[i], orig.m[i], 1e-4f) << "element " << i;
    // ...and its parent is b now.
    EXPECT_EQ(item->get_parent(), b);
}

// node_3d.cpp Node3D::set_global_position / get_global_position (4.1.4-stable):
// the global transform with its origin replaced; rotation and scale stay.
// HeavenManager.Fly (v2.2) moves the hidden door bracket this way.
TEST(GodotNode, SetGlobalPositionMovesTheOriginUnderAMovedParent) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    godot::Node3D* parent = Add3D(t, "parent", root);
    parent->set_position({5.0f, 0.0f, 0.0f});
    parent->set_rotation({0.0f, 1.5707964f, 0.0f});
    godot::Node3D* child = Add3D(t, "child", parent);
    child->set_position({1.0f, 2.0f, 3.0f});
    const core::Mat4 before = child->get_global_transform();

    child->set_global_position({0.0f, 0.0f, -100.0f});

    const godot::Vector3 at = child->get_global_position();
    EXPECT_NEAR(at.x, 0.0f, 1e-4f);
    EXPECT_NEAR(at.y, 0.0f, 1e-4f);
    EXPECT_NEAR(at.z, -100.0f, 1e-4f);
    const core::Mat4& now = child->get_global_transform();
    for (int i = 0; i < 12; ++i) EXPECT_NEAR(now.m[i], before.m[i], 1e-5f) << "basis element " << i;
}

// node.cpp Node::get_child (4.1.4-stable): a negative index counts from the
// end, and an index out of range is ERR_FAIL_INDEX_V(..., nullptr). The .gd
// relies on the null: ItemManager.SetupItemClear asks `get_child(0) is
// PickupIndicator` of every child of itemSpawnParent.
TEST(GodotNode, GetChildOutOfRangeIsNullAndNegativeCountsFromTheEnd) {
    godot::SceneTree t;
    godot::Node3D* parent = Add3D(t, "parent", t.get_root());
    godot::Node3D* a = Add3D(t, "a", parent);
    godot::Node3D* b = Add3D(t, "b", parent);

    EXPECT_EQ(parent->get_child(-1), b);
    EXPECT_EQ(parent->get_child(-2), a);
    EXPECT_EQ(parent->get_child(2), nullptr);
    EXPECT_EQ(parent->get_child(-3), nullptr);
    EXPECT_EQ(a->get_child(0), nullptr);
}

// node_3d.cpp (4.1.4-stable): set_transform marks the euler angles dirty and
// get_rotation re-derives them from the basis (Basis::get_euler_normalized,
// YXZ). `node.transform.origin = v` in GDScript is set_transform. So a Y
// rotation set as 190 degrees reads back as -170 after such a write, and a
// lerp that starts from it takes a different path (use_handcuffs_b2r1).
TEST(GodotNode, AnOriginWriteThroughTransformRederivesTheEuler) {
    godot::SceneTree t;
    godot::Node3D* n = Add3D(t, "item", t.get_root());
    n->set_rotation_degrees({0.0f, 190.0f, 0.0f});
    EXPECT_NEAR(n->get_rotation_degrees().y, 190.0f, 1e-3f);
    n->set_position({1.0f, 2.0f, 3.0f});            // `position = v`: angles kept
    EXPECT_NEAR(n->get_rotation_degrees().y, 190.0f, 1e-3f);
    n->set_transform_origin({4.0f, 5.0f, 6.0f});    // `transform.origin = v`
    EXPECT_NEAR(n->get_rotation_degrees().y, -170.0f, 1e-3f);
    EXPECT_NEAR(n->get_position().x, 4.0f, 1e-6f);
}

// Basis::get_euler YXZ's special case: a pure X rotation reads back through
// atan2, not asin, so 120 degrees survives instead of folding to 60.
TEST(GodotNode, APureXRotationReadsBackWhole) {
    godot::SceneTree t;
    godot::Node3D* n = Add3D(t, "item", t.get_root());
    n->set_rotation_degrees({120.0f, 0.0f, 0.0f});
    n->set_transform_origin({0.0f, 0.0f, 0.0f});
    EXPECT_NEAR(n->get_rotation_degrees().x, 120.0f, 1e-3f);
}

// set_scale on a dirty node keeps the rotation the basis holds.
TEST(GodotNode, ScalingAfterATransformWriteKeepsTheRotation) {
    godot::SceneTree t;
    godot::Node3D* n = Add3D(t, "item", t.get_root());
    n->set_rotation_degrees({0.0f, 30.0f, 0.0f});
    n->set_transform_origin({1.0f, 0.0f, 0.0f});
    n->set_scale({2.0f, 2.0f, 2.0f});
    EXPECT_NEAR(n->get_rotation_degrees().y, 30.0f, 1e-3f);
    EXPECT_NEAR(n->get_scale().x, 2.0f, 1e-5f);
}
