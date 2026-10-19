// Lights an animation moves. A baked page holds a light at one place, so these
// are left out of the bake (tools/bake_room) and lit per vertex at runtime from
// where the light is now (app::MovingLight, the MOVE surface program). Shadows
// are not cast at runtime.
//
//  - light_street sweeps along the car in the ending ("looping",
//    animator_street lights: x 6.13 -> -6.27).
#pragma once

#include <string_view>

namespace assets {

inline constexpr std::string_view kMovingLight = "player vehicle parent/light_street";

inline bool IsMovingLight(std::string_view nodePath) { return nodePath == kMovingLight; }

// What the moving light may light: the car. It casts no runtime shadows, so
// unscoped it would shine through the walls onto the restroom, whose camera
// sits 6 units from it. In Godot its shadows keep it inside the car.
// Everything under the scope is a prop (tools/bake_room), so the renderer can
// choose per draw.
inline constexpr std::string_view kMovingLightScope = "player vehicle parent/";

inline bool InMovingLightScope(std::string_view nodePath) {
    return nodePath.substr(0, kMovingLightScope.size()) == kMovingLightScope;
}

}  // namespace assets
