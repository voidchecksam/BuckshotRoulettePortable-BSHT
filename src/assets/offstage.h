// Props an animation throws out of the lit room.
//
// "dealer fly away" moves `dealer model parent` from local x 20.69 to 60.26 in
// 0.2 s; no light reaches him there, so Godot's per-pixel light draws him black
// on black. "dealer return to table" jumps him to 25.96 and walks him back to
// 20.69 over 2 s, out of the dark into the table's light. The port's dealer is
// a two-pose prop whose texels hold the table's light, so unscaled he would
// stay lit at the room's edge.
//
// The bake (tools/bake_room) gives a two-pose prop in this scope a probe pivot
// at its table pose and covers its walk back with probes; at runtime
// (assets::OffstageRatio) such a prop beyond the probes is dark, inside them it
// takes the usual probe ratio.
#pragma once

#include <string_view>

namespace assets {

inline constexpr std::string_view kOffstageScope = "dealer model parent/";

inline bool InOffstageScope(std::string_view nodePath) {
    return nodePath.substr(0, kOffstageScope.size()) == kOffstageScope;
}

}  // namespace assets
