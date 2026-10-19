// GDScript's Variant, as far as the scripts need one: the arguments a METHOD
// track key or a call by name carries. It is assets::Value (the animation
// export already stores METHOD arguments that way), so a key's arguments reach
// a translated script with no conversion step. Translated scripts use real
// C++ types everywhere else; Variant only where a variable really holds
// several types.
#pragma once

#include "assets/animation.h"

#include <string>

namespace godot {

using Variant = assets::Value;

inline Variant variant_from_bool(bool b) {
    Variant v;
    v.kind = assets::ValueKind::Bool;
    v.f[0] = b ? 1.0f : 0.0f;
    return v;
}

inline Variant variant_from_string(std::string s) {
    Variant v;
    v.kind = assets::ValueKind::Str;
    v.s = std::move(s);
    return v;
}

}  // namespace godot
