// Basis read-backs Godot 4.1 uses when a Node3D's euler angles and scale are
// re-derived from its transform (node_3d.cpp _update_rotation_and_scale).
// Transcribed from core/math/basis.cpp at 4.1.4-stable, in float (real_t is
// float in the shipped build). The Mat4 is column-major: Godot's rows[r][c]
// is m[c * 4 + r].
#pragma once

#include "core/mat4.h"
#include "godot/node.h"

#include <cmath>

namespace godot::basis {

namespace detail {

inline float At(const core::Mat4& m, int r, int c) { return m.m[c * 4 + r]; }

inline float Determinant(const float b[3][3]) {
    return b[0][0] * (b[1][1] * b[2][2] - b[2][1] * b[1][2]) -
           b[1][0] * (b[0][1] * b[2][2] - b[2][1] * b[0][2]) +
           b[2][0] * (b[0][1] * b[1][2] - b[1][1] * b[0][2]);
}

}  // namespace detail

// Basis::get_scale: the column lengths, all signed by the determinant.
inline Vector3 GetScale(const core::Mat4& m) {
    float b[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) b[r][c] = detail::At(m, r, c);
    const float det = detail::Determinant(b);
    const float sign = det > 0.0f ? 1.0f : (det < 0.0f ? -1.0f : 0.0f);   // SIGN()
    auto len = [&](int c) { return std::sqrt(b[0][c] * b[0][c] + b[1][c] * b[1][c] + b[2][c] * b[2][c]); };
    return {sign * len(0), sign * len(1), sign * len(2)};
}

// Basis::get_euler_normalized(YXZ), radians: Gram-Schmidt orthonormalize the
// columns, negate if the determinant is negative, then get_euler's YXZ case.
inline Vector3 GetEulerNormalizedYXZ(const core::Mat4& m) {
    float x[3], y[3], z[3];
    for (int r = 0; r < 3; ++r) {
        x[r] = detail::At(m, r, 0);
        y[r] = detail::At(m, r, 1);
        z[r] = detail::At(m, r, 2);
    }
    auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto normalize = [&](float* v) {
        const float l = std::sqrt(dot(v, v));
        if (l != 0.0f) { v[0] /= l; v[1] /= l; v[2] /= l; }
    };
    normalize(x);
    const float xy = dot(x, y);
    for (int i = 0; i < 3; ++i) y[i] = y[i] - x[i] * xy;
    normalize(y);
    const float xz = dot(x, z);
    const float yz = dot(y, z);
    for (int i = 0; i < 3; ++i) z[i] = z[i] - x[i] * xz - y[i] * yz;
    normalize(z);

    float rows[3][3];
    for (int r = 0; r < 3; ++r) {
        rows[r][0] = x[r];
        rows[r][1] = y[r];
        rows[r][2] = z[r];
    }
    if (detail::Determinant(rows) < 0.0f) {
        for (auto& row : rows)
            for (float& v : row) v = -v;
    }

    constexpr float kCmpEpsilon = 0.00001f;   // math_defs.h CMP_EPSILON
    constexpr float kHalfPi = 3.14159265358979323846f * 0.5f;
    Vector3 euler;
    const float m12 = rows[1][2];
    if (m12 < (1.0f - kCmpEpsilon)) {
        if (m12 > -(1.0f - kCmpEpsilon)) {
            // is this a pure X rotation?
            if (rows[1][0] == 0 && rows[0][1] == 0 && rows[0][2] == 0 && rows[2][0] == 0 && rows[0][0] == 1) {
                euler.x = std::atan2(-m12, rows[1][1]);
                euler.y = 0;
                euler.z = 0;
            } else {
                euler.x = std::asin(-m12);
                euler.y = std::atan2(rows[0][2], rows[2][2]);
                euler.z = std::atan2(rows[1][0], rows[1][1]);
            }
        } else {   // m12 == -1
            euler.x = kHalfPi;
            euler.y = std::atan2(rows[0][1], rows[0][0]);
            euler.z = 0;
        }
    } else {   // m12 == 1
        euler.x = -kHalfPi;
        euler.y = -std::atan2(rows[0][1], rows[0][0]);
        euler.z = 0;
    }
    return euler;
}

}  // namespace godot::basis
