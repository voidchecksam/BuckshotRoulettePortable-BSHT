// Godot 4.1.4 maths the animation system uses, transcribed from
// core/math/math_funcs.h/.cpp, core/math/quaternion.cpp and core/math/basis.cpp.
//
// Quaternion work is done in float, as Godot's real_t is float in official
// builds: Basis::get_euler compares matrix entries to exact values, and double
// arithmetic can land on the other side of those comparisons.
#pragma once

#include "godot/node.h"

#include <cmath>

namespace godot {

inline constexpr double kCmpEpsilon = 0.00001;

// Math::is_equal_approx(double, double).
inline bool is_equal_approx(double a, double b) {
    if (a == b) return true;
    double tolerance = kCmpEpsilon * std::fabs(a);
    if (tolerance < kCmpEpsilon) tolerance = kCmpEpsilon;
    return std::fabs(a - b) < tolerance;
}

inline bool is_zero_approx(double s) { return std::fabs(s) < kCmpEpsilon; }

// Math::ease (math_funcs.cpp:95-118).
inline double ease(double x, double c) {
    if (x < 0.0) {
        x = 0.0;
    } else if (x > 1.0) {
        x = 1.0;
    }
    if (c > 0.0) {
        if (c < 1.0) return 1.0 - std::pow(1.0 - x, 1.0 / c);
        return std::pow(x, c);
    }
    if (c < 0.0) {
        if (x < 0.5) return std::pow(x * 2.0, -c) * 0.5;
        return (1.0 - std::pow(1.0 - (x - 0.5) * 2.0, -c)) * 0.5 + 0.5;
    }
    return 0.0;
}

inline double lerp(double a, double b, double w) { return a + (b - a) * w; }

// Math::fposmod.
inline double fposmod(double x, double y) {
    double value = std::fmod(x, y);
    if ((value < 0.0 && y > 0.0) || (value > 0.0 && y < 0.0)) value += y;
    return value + 0.0;
}

// Math::posmod(int).
inline int posmod(int x, int y) {
    int value = x % y;
    if ((value < 0 && y > 0) || (value > 0 && y < 0)) value += y;
    return value;
}

// Math::cubic_interpolate_in_time, the Barry-Goldman method (math_funcs.h:293).
inline double cubic_interpolate_in_time(double from, double to, double pre, double post,
                                        double weight, double to_t, double pre_t, double post_t) {
    const double t = lerp(0.0, to_t, weight);
    const double a1 = lerp(pre, from, pre_t == 0.0 ? 0.0 : (t - pre_t) / -pre_t);
    const double a2 = lerp(from, to, to_t == 0.0 ? 0.5 : t / to_t);
    const double a3 = lerp(to, post, post_t - to_t == 0.0 ? 1.0 : (t - to_t) / (post_t - to_t));
    const double b1 = lerp(a1, a2, to_t - pre_t == 0.0 ? 0.0 : (t - pre_t) / (to_t - pre_t));
    const double b2 = lerp(a2, a3, post_t == 0.0 ? 1.0 : t / post_t);
    return lerp(b1, b2, to_t == 0.0 ? 0.5 : t / to_t);
}

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

// Quaternion::slerp (quaternion.cpp:113-152), including the shortest-path flip
// and the near-parallel linear fallback.
inline Quaternion slerp(const Quaternion& from, const Quaternion& to, float weight) {
    const float kEps = static_cast<float>(kCmpEpsilon);
    float cosom = from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w;
    Quaternion to1 = to;
    if (cosom < 0.0f) {
        cosom = -cosom;
        to1 = {-to.x, -to.y, -to.z, -to.w};
    }
    float scale0 = 1.0f - weight;
    float scale1 = weight;
    if ((1.0f - cosom) > kEps) {
        const float omega = std::acos(cosom);
        const float sinom = std::sin(omega);
        scale0 = static_cast<float>(std::sin((1.0 - weight) * omega)) / sinom;
        scale1 = std::sin(weight * omega) / sinom;
    }
    return {scale0 * from.x + scale1 * to1.x, scale0 * from.y + scale1 * to1.y,
            scale0 * from.z + scale1 * to1.z, scale0 * from.w + scale1 * to1.w};
}

// Basis(Quaternion) (Basis::set_quaternion) then Basis::get_euler(YXZ),
// basis.cpp:500-535. Node3D.set_rotation(quaternion.get_euler()) is how a
// ROTATION_3D track reaches a node.
inline Vector3 quat_to_euler_yxz(const Quaternion& q) {
    const float kEps = static_cast<float>(kCmpEpsilon);
    const float kHalfPi = 3.14159265358979323846f * 0.5f;
    const float d = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    const float s = 2.0f / d;
    const float xs = q.x * s, ys = q.y * s, zs = q.z * s;
    const float wx = q.w * xs, wy = q.w * ys, wz = q.w * zs;
    const float xx = q.x * xs, xy = q.x * ys, xz = q.x * zs;
    const float yy = q.y * ys, yz = q.y * zs, zz = q.z * zs;
    const float r00 = 1.0f - (yy + zz), r01 = xy - wz, r02 = xz + wy;
    const float r10 = xy + wz, r11 = 1.0f - (xx + zz), r12 = yz - wx;
    const float r20 = xz - wy, r22 = 1.0f - (xx + yy);

    Vector3 e;
    if (r12 < (1.0f - kEps)) {
        if (r12 > -(1.0f - kEps)) {
            if (r10 == 0.0f && r01 == 0.0f && r02 == 0.0f && r20 == 0.0f && r00 == 1.0f) {
                // A pure X rotation: Godot returns the simplest form.
                e.x = std::atan2(-r12, r11);
            } else {
                e.x = std::asin(-r12);
                e.y = std::atan2(r02, r22);
                e.z = std::atan2(r10, r11);
            }
        } else {
            e.x = kHalfPi;
            e.y = std::atan2(r01, r00);
        }
    } else {
        e.x = -kHalfPi;
        e.y = -std::atan2(r01, r00);
    }
    return e;
}

}  // namespace godot
