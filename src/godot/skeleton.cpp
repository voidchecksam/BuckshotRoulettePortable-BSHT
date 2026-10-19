#include "godot/skeleton.h"

#include <cmath>

namespace godot {

Quaternion QuaternionFromMatrix(const core::Mat4& m) {
    // Columns are the basis axes; element (row r, column c) is m[c * 4 + r].
    const float m00 = m.m[0], m01 = m.m[4], m02 = m.m[8];
    const float m10 = m.m[1], m11 = m.m[5], m12 = m.m[9];
    const float m20 = m.m[2], m21 = m.m[6], m22 = m.m[10];
    const float trace = m00 + m11 + m22;
    Quaternion q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return q;
}

core::Mat4 ComposePose(const Vector3& p, const Quaternion& q, const Vector3& s) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    core::Mat4 r;
    r.m[0] = (1 - 2 * (y * y + z * z)) * s.x;
    r.m[1] = (2 * (x * y + z * w)) * s.x;
    r.m[2] = (2 * (x * z - y * w)) * s.x;
    r.m[4] = (2 * (x * y - z * w)) * s.y;
    r.m[5] = (1 - 2 * (x * x + z * z)) * s.y;
    r.m[6] = (2 * (y * z + x * w)) * s.y;
    r.m[8] = (2 * (x * z + y * w)) * s.z;
    r.m[9] = (2 * (y * z - x * w)) * s.z;
    r.m[10] = (1 - 2 * (x * x + y * y)) * s.z;
    r.m[12] = p.x;
    r.m[13] = p.y;
    r.m[14] = p.z;
    return r;
}

void Skeleton3D::add_bone(std::string name, int parent, const core::Mat4& rest, const core::Mat4& pose) {
    Bone b;
    b.name = std::move(name);
    b.parent = parent;
    b.rest = rest;
    b.position = {pose.m[12], pose.m[13], pose.m[14]};
    float sc[3];
    for (int c = 0; c < 3; ++c) {
        sc[c] = std::sqrt(pose.m[c * 4] * pose.m[c * 4] + pose.m[c * 4 + 1] * pose.m[c * 4 + 1] +
                          pose.m[c * 4 + 2] * pose.m[c * 4 + 2]);
    }
    b.scale = {sc[0], sc[1], sc[2]};
    core::Mat4 unit = pose;
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) unit.m[c * 4 + r] /= sc[c] > 0.0f ? sc[c] : 1.0f;
    b.rotation = QuaternionFromMatrix(unit);
    bones_.push_back(std::move(b));
    dirty_ = true;
    ++version_;
}

int Skeleton3D::find_bone(const std::string& name) const {
    for (std::size_t i = 0; i < bones_.size(); ++i) {
        if (bones_[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void Skeleton3D::set_bone_pose_position(int bone, const Vector3& p) {
    if (bone < 0 || bone >= get_bone_count()) return;
    bones_[static_cast<std::size_t>(bone)].position = p;
    dirty_ = true;
    ++version_;
}

void Skeleton3D::set_bone_pose_rotation(int bone, const Quaternion& q) {
    if (bone < 0 || bone >= get_bone_count()) return;
    bones_[static_cast<std::size_t>(bone)].rotation = q;
    dirty_ = true;
    ++version_;
}

void Skeleton3D::set_bone_pose_scale(int bone, const Vector3& s) {
    if (bone < 0 || bone >= get_bone_count()) return;
    bones_[static_cast<std::size_t>(bone)].scale = s;
    dirty_ = true;
    ++version_;
}

// Bones are listed parents first (Skeleton3D's process order for an imported
// skeleton), so one pass in index order resolves every parent.
void Skeleton3D::Update() const {
    if (!dirty_) return;
    for (Bone& b : bones_) {
        const core::Mat4 local = ComposePose(b.position, b.rotation, b.scale);
        const bool hasParent = b.parent >= 0 && b.parent < static_cast<int>(bones_.size());
        b.global = hasParent ? core::Mul(bones_[static_cast<std::size_t>(b.parent)].global, local) : local;
    }
    dirty_ = false;
}

const core::Mat4& Skeleton3D::get_bone_global_pose(int bone) const {
    Update();
    return bones_[static_cast<std::size_t>(bone)].global;
}

}  // namespace godot
