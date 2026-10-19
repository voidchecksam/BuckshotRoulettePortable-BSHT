// Skeleton3D and a mesh's Skin, the parts the skinned figures use: bones with
// a rest and a pose (position / rotation / scale, set by an animation's bone
// tracks), and each bone's global pose in skeleton space
// (skeleton_3d.cpp: pose_global = parent's pose_global * pose).
//
// The renderer skins a mesh with bone_matrix[i] = pose_global(bind i's bone)
// * bind_pose(i) (skeleton_3d.cpp:296), in the MeshInstance3D's space.
#pragma once

#include "core/mat4.h"
#include "godot/math.h"
#include "godot/node.h"

#include <cstdint>
#include <string>
#include <vector>

namespace godot {

class Skeleton3D : public Node3D {
public:
    static constexpr uint32_t kClassBit = kClassSkeleton3D;
    explicit Skeleton3D(std::string name) : Node3D(std::move(name), kClassSkeleton3D) { set_class("Skeleton3D"); }

    // `pose` is the bone's pose at export, split into position, rotation and
    // scale as Skeleton3D keeps it.
    void add_bone(std::string name, int parent, const core::Mat4& rest, const core::Mat4& pose);
    int get_bone_count() const { return static_cast<int>(bones_.size()); }
    int find_bone(const std::string& name) const;
    const std::string& get_bone_name(int bone) const { return bones_[static_cast<std::size_t>(bone)].name; }

    void set_bone_pose_position(int bone, const Vector3& p);
    void set_bone_pose_rotation(int bone, const Quaternion& q);
    void set_bone_pose_scale(int bone, const Vector3& s);
    Vector3 get_bone_pose_position(int bone) const { return bones_[static_cast<std::size_t>(bone)].position; }

    // Skeleton space; recomputed after any pose change.
    const core::Mat4& get_bone_global_pose(int bone) const;
    // Moves whenever a pose changes: a skinned mesh re-skins only then.
    uint32_t get_pose_version() const { return version_; }

private:
    struct Bone {
        std::string name;
        int parent = -1;
        core::Mat4 rest;
        Vector3 position;
        Quaternion rotation;
        Vector3 scale{1.0f, 1.0f, 1.0f};
        core::Mat4 global;
    };
    void Update() const;
    mutable std::vector<Bone> bones_;
    mutable bool dirty_ = true;
    uint32_t version_ = 0;
};

// Basis -> quaternion (Basis::get_quaternion, basis.cpp) for an orthonormal basis.
Quaternion QuaternionFromMatrix(const core::Mat4& m);
// Translation * rotation * scale (Transform3D(Basis(q).scaled(s), p)).
core::Mat4 ComposePose(const Vector3& p, const Quaternion& q, const Vector3& s);

}  // namespace godot
