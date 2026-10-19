// The animated lights: they sit at energy 0 at rest and burn for a moment (the
// muzzle flash, the two masks, the zippo). Each is a vertex channel the bake
// fills per vertex where it burns, scaled by its live energy at runtime (the
// DYN program); none is ever part of the static bake, whatever pose a reveal
// leaves it at. The order is the channel order the bake writes and the shader
// reads: R, G, B, A. Names are v2.2's.
//
//   if (assets::BakesAsStaticLight(path, light.is_visible_in_tree(), energy)) bake it;
#pragma once

#include "assets/moving_lights.h"

#include <array>
#include <string_view>

namespace assets {

inline constexpr std::array<std::string_view, 4> kDynLightPaths = {
    "tabletop parent/shotgun main parent/light_muzzle flash LS",
    "light parent/light_barrel mask LS",
    "light parent/light_shotgun inspect mask LS",
    "player item interaction parent/zippo lighter parent2/light_zippo flame",
};

inline bool IsDynLight(std::string_view nodePath) {
    for (std::string_view p : kDynLightPaths) {
        if (p == nodePath) return true;
    }
    return false;
}

// Whether a light belongs in the static bake: visible, burning, and neither the
// moving light (lit per vertex where it is now) nor an animated one. A reveal
// can leave an animated light burning (the zippo, 2.0 s into "player use
// cigarettes"), and once baked it would never go out.
inline bool BakesAsStaticLight(std::string_view nodePath, bool visibleInTree, float energy) {
    return visibleInTree && energy > 0.0f && !IsMovingLight(nodePath) && !IsDynLight(nodePath);
}

}  // namespace assets
