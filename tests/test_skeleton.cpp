// Skeleton3D global poses and CPU skinning.
#include "assets/skinning.h"
#include "godot/runtime_scene_loader.h"
#include "godot/skeleton.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

TEST(Skeleton, AChildBoneFollowsItsParent) {
    godot::Skeleton3D sk("Skeleton3D");
    core::Mat4 up;
    up.m[13] = 1.0f;   // one unit up
    sk.add_bone("root", -1, core::Mat4{}, core::Mat4{});
    sk.add_bone("tip", 0, up, up);
    EXPECT_NEAR(sk.get_bone_global_pose(1).m[13], 1.0f, 1e-6f);
    const uint32_t before = sk.get_pose_version();
    sk.set_bone_pose_position(0, {2.0f, 0.0f, 0.0f});
    EXPECT_NE(sk.get_pose_version(), before);
    EXPECT_NEAR(sk.get_bone_global_pose(1).m[12], 2.0f, 1e-6f);
    EXPECT_NEAR(sk.get_bone_global_pose(1).m[13], 1.0f, 1e-6f);
}

TEST(Skeleton, ARotationRoundTripsThroughTheMatrix) {
    const godot::Quaternion q{0.0f, std::sin(0.4f), 0.0f, std::cos(0.4f)};
    const godot::Quaternion back = godot::QuaternionFromMatrix(godot::ComposePose({0, 0, 0}, q, {1, 1, 1}));
    EXPECT_NEAR(back.y, q.y, 1e-5f);
    EXPECT_NEAR(back.w, q.w, 1e-5f);
}

TEST(Skinning, WeightsBlendTheBonesTransforms) {
    assets::Mesh m;
    m.positions = {0.0f, 0.0f, 0.0f};
    m.joints = {0, 1, 0, 0};
    m.weights = {0.5f, 0.5f, 0.0f, 0.0f};
    core::Mat4 a, b;
    a.m[12] = 2.0f;
    b.m[12] = 4.0f;
    std::vector<float> out;
    assets::SkinPositions(m, {a, b}, out);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_NEAR(out[0], 3.0f, 1e-6f);
}

// The real export: every bone's parent comes before it (one pass resolves
// global poses), and every skin bind names a bone of its skeleton.
TEST(Skeleton, TheExportedSkinsBindToTheirSkeletons) {
    const std::string path = std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_main.json";
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(path.c_str(), t) != godot::RuntimeSceneError::None) GTEST_SKIP();
    int skins = 0;
    for (godot::Node* n : t.nodes_in_tree_order()) {
        if (auto* sk = godot::cast_to<godot::Skeleton3D>(n)) {
            for (int i = 0; i < sk->get_bone_count(); ++i) {
                (void)sk->get_bone_global_pose(i);
            }
        }
        const auto* mi = godot::cast_to<godot::MeshInstance3D>(n);
        if (!mi || !mi->has_skin()) continue;
        const auto* sk = godot::cast_to<godot::Skeleton3D>(t.get_node_relative(mi, mi->get_skeleton_path()));
        ASSERT_NE(sk, nullptr) << mi->get_path();
        for (const auto& b : mi->get_skin_binds()) EXPECT_GE(sk->find_bone(b.name), 0) << b.name;
        ++skins;
    }
    if (skins == 0) GTEST_SKIP() << "runtime_main.json predates the skin keys";
    EXPECT_EQ(skins, 3);
}

