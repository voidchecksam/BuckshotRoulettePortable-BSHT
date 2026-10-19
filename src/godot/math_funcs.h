// GDScript's global maths the translated scripts call, transcribed from
// core/math/math_funcs.{h,cpp} at 4.1.4-stable. ease() and lerp(double) live in
// godot/math.h, shared with the animation system; this adds what only scripts
// use. Doubles, as GDScript's float is a double.
#pragma once

#include "godot/math.h"
#include "godot/node.h"

#include <cmath>

namespace godot {

inline double clampf(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline double lerpf(double from, double to, double weight) { return lerp(from, to, weight); }

// Vector3.lerp: per component in real_t (float).
inline Vector3 lerp(const Vector3& from, const Vector3& to, double weight) {
    const float w = static_cast<float>(weight);
    return {from.x + w * (to.x - from.x), from.y + w * (to.y - from.y), from.z + w * (to.z - from.z)};
}

// math_funcs.h:437 / 444.
inline double linear_to_db(double linear) { return std::log(linear) * 8.6858896380650365530225783783321; }
inline double db_to_linear(double db) { return std::exp(db * 0.11512925464970228420089957273422); }

}  // namespace godot
