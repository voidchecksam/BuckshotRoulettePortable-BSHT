// A node's local transform as Godot holds it: position, rotation in YXZ order,
// and scale, and back again. godot::Node3D composes its local matrix with
// ComposeTRSRad.
//
// Headless, so the round trip is testable against every node in the room.
#pragma once

#include "core/mat4.h"

#include <cmath>

namespace core {

// scale, then R = Ry * Rx * Rz (Godot's YXZ), then translation. Radians, as
// Godot's Node3D.rotation stores them. Column-major: m[col * 4 + row].
inline Mat4 ComposeTRSRad(const float position[3], const float rotationRad[3],
                          const float scale[3]) {
    const float sx = std::sin(rotationRad[0]), cx = std::cos(rotationRad[0]);
    const float sy = std::sin(rotationRad[1]), cy = std::cos(rotationRad[1]);
    const float sz = std::sin(rotationRad[2]), cz = std::cos(rotationRad[2]);

    const float m00 =  cy * cz + sy * sx * sz;
    const float m01 = -cy * sz + sy * sx * cz;
    const float m02 =  sy * cx;
    const float m10 =  cx * sz;
    const float m11 =  cx * cz;
    const float m12 = -sx;
    const float m20 = -sy * cz + cy * sx * sz;
    const float m21 =  sy * sz + cy * sx * cz;
    const float m22 =  cy * cx;

    Mat4 r;
    r.m[0] = m00 * scale[0]; r.m[1] = m10 * scale[0]; r.m[2]  = m20 * scale[0]; r.m[3]  = 0.0f;
    r.m[4] = m01 * scale[1]; r.m[5] = m11 * scale[1]; r.m[6]  = m21 * scale[1]; r.m[7]  = 0.0f;
    r.m[8] = m02 * scale[2]; r.m[9] = m12 * scale[2]; r.m[10] = m22 * scale[2]; r.m[11] = 0.0f;
    r.m[12] = position[0];   r.m[13] = position[1];   r.m[14] = position[2];    r.m[15] = 1.0f;
    return r;
}

// Degrees, as rotation_degrees: the round-trip tests' pair with DecomposeTRS.
inline Mat4 ComposeTRS(const float position[3], const float rotationDeg[3],
                       const float scale[3]) {
    const float kDeg = 3.14159265358979323846f / 180.0f;
    const float rad[3] = {rotationDeg[0] * kDeg, rotationDeg[1] * kDeg, rotationDeg[2] * kDeg};
    return ComposeTRSRad(position, rad, scale);
}

// The inverse of ComposeTRS for any rotation * scale basis. A mirrored basis
// (negative determinant) is expressed as a negative X scale, as Godot does. A
// sheared basis cannot be expressed as position/rotation/scale at all, in Godot
// or here; callers that must be exact keep the rest matrix instead.
inline void DecomposeTRS(const Mat4& m, float position[3], float rotationDeg[3],
                         float scale[3]) {
    position[0] = m.m[12];
    position[1] = m.m[13];
    position[2] = m.m[14];

    float c0[3] = {m.m[0], m.m[1], m.m[2]};
    float c1[3] = {m.m[4], m.m[5], m.m[6]};
    float c2[3] = {m.m[8], m.m[9], m.m[10]};

    scale[0] = std::sqrt(c0[0] * c0[0] + c0[1] * c0[1] + c0[2] * c0[2]);
    scale[1] = std::sqrt(c1[0] * c1[0] + c1[1] * c1[1] + c1[2] * c1[2]);
    scale[2] = std::sqrt(c2[0] * c2[0] + c2[1] * c2[1] + c2[2] * c2[2]);

    const float det = c0[0] * (c1[1] * c2[2] - c1[2] * c2[1]) -
                      c1[0] * (c0[1] * c2[2] - c0[2] * c2[1]) +
                      c2[0] * (c0[1] * c1[2] - c0[2] * c1[1]);
    if (det < 0.0f) scale[0] = -scale[0];

    for (int i = 0; i < 3; ++i) {
        if (scale[0] != 0.0f) c0[i] /= scale[0];
        if (scale[1] != 0.0f) c1[i] /= scale[1];
        if (scale[2] != 0.0f) c2[i] /= scale[2];
    }

    // Unit rotation matrix, row-by-column: rRC = column C, row R.
    const float r02 = c2[0], r12 = c2[1], r22 = c2[2];
    const float r10 = c0[1], r11 = c1[1];
    const float r00 = c0[0], r20 = c0[2];

    const float kRad = 180.0f / 3.14159265358979323846f;
    float s = -r12;
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    const float x = std::asin(s);

    float y = 0.0f;
    float z = 0.0f;
    if (std::fabs(s) < 0.9999f) {
        y = std::atan2(r02, r22);
        z = std::atan2(r10, r11);
    } else {
        // Gimbal lock: X is +-90, and Y and Z rotate about the same axis.
        // Fold everything into Y.
        y = std::atan2(-r20, r00);
        z = 0.0f;
    }

    rotationDeg[0] = x * kRad;
    rotationDeg[1] = y * kRad;
    rotationDeg[2] = z * kRad;
}

}  // namespace core
