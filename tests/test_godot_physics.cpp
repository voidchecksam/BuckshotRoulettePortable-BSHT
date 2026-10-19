// Ray queries from Godot 4.1.4-stable:
//   godot_space_3d.cpp  GodotPhysicsDirectSpaceState3D::intersect_ray
//   godot_shape_3d.cpp  GodotBoxShape3D::intersect_segment:420,
//                       GodotConcavePolygonShape3D::_setup / _cull_segment:1348 /
//                       intersect_segment:1384, GodotFaceShape3D::intersect_segment
//   aabb.cpp            AABB::intersects_segment
//   geometry_3d.h       Geometry3D::segment_intersects_triangle
//   camera_3d.cpp       project_ray_origin:321, project_position:393
#include "godot/camera_projection.h"
#include "godot/physics.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

godot::StaticBody3D* AddBoxBody(godot::SceneTree& t, godot::Node* parent, const char* name, godot::Vector3 at,
                               godot::Vector3 size) {
    auto* body =
        static_cast<godot::StaticBody3D*>(t.add_node(std::make_unique<godot::StaticBody3D>(name), parent));
    body->set_position(at);
    auto* cs = static_cast<godot::CollisionShape3D*>(
        t.add_node(std::make_unique<godot::CollisionShape3D>("CollisionShape3D"), body));
    auto box = std::make_shared<godot::BoxShape3D>();
    box->half_extents = {size.x / 2, size.y / 2, size.z / 2};
    cs->shape = box;
    return body;
}

godot::RayQuery Ray(godot::Vector3 from, godot::Vector3 to) {
    godot::RayQuery q;
    q.from = from;
    q.to = to;
    return q;
}

}  // namespace

TEST(GodotPhysics, RayHitsTheNearestBoxAndReportsItsBody) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    AddBoxBody(t, root, "far", {0, 0, -10}, {2, 2, 2});
    godot::StaticBody3D* nearBody = AddBoxBody(t, root, "near", {0, 0, -5}, {2, 2, 2});
    t.physics_sync();
    godot::RayResult r;
    ASSERT_TRUE(t.physics_space().intersect_ray(Ray({0, 0, 0}, {0, 0, -20}), r));
    EXPECT_EQ(r.collider, nearBody);
    EXPECT_NEAR(r.position.z, -4.0f, 1e-5f);
    EXPECT_NEAR(r.normal.z, 1.0f, 1e-5f);
}

TEST(GodotPhysics, NoBodyOnTheRayIsNoHit) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    AddBoxBody(t, root, "aside", {5, 0, -5}, {2, 2, 2});
    t.physics_sync();
    godot::RayResult r;
    EXPECT_FALSE(t.physics_space().intersect_ray(Ray({0, 0, 0}, {0, 0, -20}), r));
    EXPECT_EQ(r.collider, nullptr);
}

// DecisionTextManager sets the text colliders' layer to 0 and back to 1; the
// space only sees a change after the next sync (Godot's physics step).
TEST(GodotPhysics, LayerZeroIsNotHitAndChangesApplyAfterSync) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    godot::StaticBody3D* b = AddBoxBody(t, root, "text dealer", {0, 0, -5}, {2, 2, 2});
    b->collision_layer = 0;
    t.physics_sync();
    godot::RayResult r;
    EXPECT_FALSE(t.physics_space().intersect_ray(Ray({0, 0, 0}, {0, 0, -20}), r));
    b->collision_layer = 1;
    EXPECT_FALSE(t.physics_space().intersect_ray(Ray({0, 0, 0}, {0, 0, -20}), r)) << "not synced yet";
    t.physics_sync();
    EXPECT_TRUE(t.physics_space().intersect_ray(Ray({0, 0, 0}, {0, 0, -20}), r));
}

// intersect_ray: hit_from_inside defaults false, so a shape containing the ray
// origin is skipped.
TEST(GodotPhysics, StartingInsideAShapeIsIgnoredByDefault) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    AddBoxBody(t, root, "around", {0, 0, 0}, {4, 4, 4});
    t.physics_sync();
    godot::RayResult r;
    EXPECT_FALSE(t.physics_space().intersect_ray(Ray({0, 0, 0}, {0, 0, -20}), r));
}

TEST(GodotPhysics, DisabledShapeIsNotHit) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    godot::StaticBody3D* b = AddBoxBody(t, root, "off", {0, 0, -5}, {2, 2, 2});
    godot::cast_to<godot::CollisionShape3D>(b->get_child(0))->disabled = true;
    t.physics_sync();
    godot::RayResult r;
    EXPECT_FALSE(t.physics_space().intersect_ray(Ray({0, 0, 0}, {0, 0, -20}), r));
}

// A box rotated and scaled through its body: the query runs in shape space.
TEST(GodotPhysics, RotatedBodyIsHitInWorldSpace) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    godot::StaticBody3D* b = AddBoxBody(t, root, "turned", {0, 0, -5}, {4, 0.2f, 0.2f});
    b->set_rotation({0.0f, 1.5707963f, 0.0f});   // the long axis now runs along z
    t.physics_sync();
    godot::RayResult r;
    ASSERT_TRUE(t.physics_space().intersect_ray(Ray({0, 0, 0}, {0, 0, -20}), r));
    EXPECT_NEAR(r.position.z, -3.0f, 1e-4f);
}

// GodotFaceShape3D::intersect_segment: a back-face hit counts only with
// backface_collision (and hit_back_faces, true by default). The winding below
// makes Face3::get_plane's normal point +z, toward a ray from the origin.
TEST(GodotPhysics, ConcaveBackFaceIsHitOnlyWithBackfaceCollision) {
    auto shape = std::make_shared<godot::ConcavePolygonShape3D>();
    shape->set_faces({{-1, -1, -5}, {0, 1, -5}, {1, -1, -5}});
    godot::Vector3 p;
    godot::Vector3 n;
    ASSERT_TRUE(shape->intersect_segment({0, 0, 0}, {0, 0, -20}, p, n, true)) << "front face";
    EXPECT_NEAR(p.z, -5.0f, 1e-5f);
    EXPECT_FALSE(shape->intersect_segment({0, 0, -20}, {0, 0, 0}, p, n, true)) << "back face, no backface_collision";
    shape->backface_collision = true;
    EXPECT_TRUE(shape->intersect_segment({0, 0, -20}, {0, 0, 0}, p, n, true));
}

TEST(GodotPhysics, ConcaveNearestFaceWinsAcrossTheBvh) {
    auto shape = std::make_shared<godot::ConcavePolygonShape3D>();
    std::vector<godot::Vector3> faces;
    for (int k = 1; k <= 20; ++k) {
        const float z = -static_cast<float>(k);
        faces.push_back({-1, -1, z});
        faces.push_back({0, 1, z});
        faces.push_back({1, -1, z});
    }
    shape->set_faces(faces);
    godot::Vector3 p;
    godot::Vector3 n;
    ASSERT_TRUE(shape->intersect_segment({0, 0, 0}, {0, 0, -100}, p, n, true));
    EXPECT_NEAR(p.z, -1.0f, 1e-5f);
}

TEST(GodotPhysics, SegmentMissingEveryFaceIsNoHit) {
    auto shape = std::make_shared<godot::ConcavePolygonShape3D>();
    shape->set_faces({{-1, -1, -5}, {0, 1, -5}, {1, -1, -5}});
    godot::Vector3 p;
    godot::Vector3 n;
    EXPECT_FALSE(shape->intersect_segment({5, 5, 0}, {5, 5, -20}, p, n, true));
    EXPECT_FALSE(shape->intersect_segment({0, 0, 0}, {0, 0, -4}, p, n, true)) << "segment ends before the face";
}

// The real scene at the moment Godot 4.1.4 focuses the shotgun bracket
// (measured with --fixed-fps 60): the camera's global transform, what Camera3D
// projects for ButtonClass_Main's mouseRaycastVector (453,175), and what
// intersect_ray hits.
TEST(GodotPhysics, MainSceneShotgunOverrideRayMatchesGodot) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(BSHT_SOURCE_DIR "/assets/extracted/runtime_main.json", t) !=
        godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "runtime_main.json not exported";
    }
    auto* cam = t.get_node_as<godot::Camera3D>("Camera");
    ASSERT_NE(cam, nullptr);
    // Column-major: basis x, y, z columns then origin (Godot basis.x/.y/.z).
    const core::Mat4 xform{{-0.00000004371139f, 0.0f, 1.0f, 0.0f,
                            0.75470954179764f, 0.65605902671814f, 0.0000000329894f, 0.0f,
                            -0.65605902671814f, 0.75470954179764f, -0.00000002867725f, 0.0f,
                            -13.5900001525879f, 7.32299995422363f, 0.02400000020862f, 1.0f}};
    cam->set_rest(xform, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    EXPECT_NEAR(cam->get_near(), 0.05f, 1e-6f);
    EXPECT_NEAR(cam->get_far(), 4000.0f, 1e-3f);

    const godot::Vector3 from = godot::project_ray_origin(*cam, {453, 175}, godot::kViewportSize);
    const godot::Vector3 to = godot::project_position(*cam, {453, 175}, 20000.0f, godot::kViewportSize);
    EXPECT_NEAR(from.x, -13.5900001525879f, 1e-4f);
    EXPECT_NEAR(from.y, 7.32299995422363f, 1e-4f);
    EXPECT_NEAR(from.z, 0.02400000020862f, 1e-4f);
    // 20000 units out, float error in Godot's own maths is a fraction of a unit.
    EXPECT_NEAR(to.x, 16523.6875f, 0.5f);
    EXPECT_NEAR(to.y, -12117.298828125f, 0.5f);
    EXPECT_NEAR(to.z, -1286.41784667969f, 0.5f);

    t.physics_sync();
    godot::RayResult r;
    ASSERT_TRUE(t.physics_space().intersect_ray(Ray(from, to), r));
    ASSERT_NE(r.collider, nullptr);
    EXPECT_EQ(r.collider->get_path(), "tabletop parent/shotgun main parent/shotgun parent new/StaticBody3D");
    // Godot's hit with nothing focused yet (probe "UNLIFTED", gun local x 0).
    // The first number taken, (-3.798, 0.144, -0.738), was sampled after focus:
    // PickupIndicator had already raised the gun ~0.13, which moves the hit
    // along this ray by exactly that. The export is frame 3, where the collider
    // sits 6.4e-4 from its player-turn pose, hence the 2e-3 tolerance.
    EXPECT_NEAR(r.position.x, -3.61625194549561f, 2e-3f);
    EXPECT_NEAR(r.position.y, 0.01055300235748f, 2e-3f);
    EXPECT_NEAR(r.position.z, -0.75186324119568f, 2e-3f);
    EXPECT_NEAR(r.normal.y, 1.0f, 1e-4f);
}

// Transform3D::affine_inverse for a non-orthogonal basis (rotation * scale with
// shear) and a translation: inverse * transform is the identity.
TEST(GodotPhysics, InverseAffineUndoesAnAffineTransform) {
    const core::Mat4 m{{2.0f, 0.5f, 0.0f, 0.0f,
                        -0.3f, 1.5f, 0.2f, 0.0f,
                        0.1f, 0.0f, 0.8f, 0.0f,
                        3.0f, -2.0f, 5.0f, 1.0f}};
    const core::Mat4 id = core::Mul(core::InverseAffine(m), m);
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(id.m[i], (i % 5 == 0) ? 1.0f : 0.0f, 1e-5f) << "element " << i;
}

// ---------------------------------------------------------- CylinderShape3D
// godot_shape_3d.cpp GodotCylinderShape3D::intersect_segment:746 (which is
// Geometry3D::segment_intersects_cylinder on axis 1, geometry_3d.h:166) and
// intersect_point:750. The beer can is the only cylinder in the game; it was
// the first shape outside main.tscn's box / concave pair.

namespace {
godot::CylinderShape3D Cylinder(float radius, float height) {
    godot::CylinderShape3D c;
    c.radius = radius;
    c.height = height;
    return c;
}
}  // namespace

TEST(GodotCylinderShape, APointIsInsideOnlyWithinTheRadiusAndTheHalfHeight) {
    const auto c = Cylinder(0.5f, 2.0f);
    EXPECT_TRUE(c.intersect_point({0.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(c.intersect_point({0.3f, 0.9f, 0.3f}));
    EXPECT_FALSE(c.intersect_point({0.6f, 0.0f, 0.0f}));   // past the radius
    EXPECT_FALSE(c.intersect_point({0.0f, 1.1f, 0.0f}));   // past the half-height
}

TEST(GodotCylinderShape, ASegmentThroughTheSideHitsTheCurvedWall) {
    const auto c = Cylinder(0.5f, 2.0f);
    godot::Vector3 hit, normal;
    ASSERT_TRUE(c.intersect_segment({-2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, hit, normal, false));
    EXPECT_NEAR(hit.x, -0.5f, 1e-4f);
    EXPECT_NEAR(hit.y, 0.0f, 1e-4f);
    EXPECT_NEAR(normal.x, -1.0f, 1e-4f);   // the wall's normal: height axis zeroed
    EXPECT_NEAR(normal.y, 0.0f, 1e-4f);
}

TEST(GodotCylinderShape, ASegmentDownTheAxisHitsTheCap) {
    const auto c = Cylinder(0.5f, 2.0f);
    godot::Vector3 hit, normal;
    ASSERT_TRUE(c.intersect_segment({0.0f, 5.0f, 0.1f}, {0.0f, -5.0f, 0.1f}, hit, normal, false));
    EXPECT_NEAR(hit.y, 1.0f, 1e-4f);
    EXPECT_NEAR(normal.y, 1.0f, 1e-4f);    // the cap's normal: side axes zeroed
}

TEST(GodotCylinderShape, ASegmentPastTheRadiusMisses) {
    const auto c = Cylinder(0.5f, 2.0f);
    godot::Vector3 hit, normal;
    EXPECT_FALSE(c.intersect_segment({-2.0f, 0.0f, 0.6f}, {2.0f, 0.0f, 0.6f}, hit, normal, false));
}
