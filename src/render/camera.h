// View and projection.
//
// The far plane is deliberately generous: the room's coordinates run to roughly
// +/-160 units (the camera node alone sits at x = -162), so a typical 100-unit
// far plane would clip most of the scene away and read as missing geometry.
#pragma once

#include "core/mat4.h"

namespace render {

class Camera {
public:
    void SetPerspective(float fovYDeg, float aspect, float nearZ, float farZ) {
        proj_ = core::Perspective(fovYDeg, aspect, nearZ, farZ);
    }

    void LookAt(const float eye[3], const float target[3]) {
        static const float kUp[3] = {0.0f, 1.0f, 0.0f};
        for (int i = 0; i < 3; ++i) eye_[i] = eye[i];
        view_ = core::LookAt(eye, target, kUp);
    }

    // The scene's Camera3D: `global` is its global transform (column-major,
    // basis columns 0-2, origin in 12-14), `fovYDeg` Godot's vertical fov
    // (keep_aspect KEEP_HEIGHT). The view is the rigid inverse; the basis is
    // normalised first so a scaled camera parent cannot scale the view.
    void SetFromGodot(const core::Mat4& global, float fovYDeg, float aspect, float nearZ, float farZ) {
        SetPerspective(fovYDeg, aspect, nearZ, farZ);
        float axis[3][3];
        for (int c = 0; c < 3; ++c) {
            for (int r = 0; r < 3; ++r) axis[c][r] = global.m[c * 4 + r];
            core::Normalize3(axis[c]);
        }
        for (int i = 0; i < 3; ++i) eye_[i] = global.m[12 + i];
        core::Mat4 v;
        for (int c = 0; c < 3; ++c) {
            for (int r = 0; r < 3; ++r) v.m[c * 4 + r] = axis[r][c];   // transpose
        }
        for (int r = 0; r < 3; ++r) {
            v.m[12 + r] = -(axis[r][0] * eye_[0] + axis[r][1] * eye_[1] + axis[r][2] * eye_[2]);
        }
        v.m[3] = v.m[7] = v.m[11] = 0.0f;
        v.m[15] = 1.0f;
        view_ = v;
    }

    const core::Mat4& View() const { return view_; }
    const core::Mat4& Proj() const { return proj_; }
    const float* Eye() const { return eye_; }

    core::Mat4 ViewProj() const { return core::Mul(proj_, view_); }

private:
    core::Mat4 view_;
    core::Mat4 proj_;
    float      eye_[3] = {0.0f, 0.0f, 0.0f};
};

}  // namespace render
