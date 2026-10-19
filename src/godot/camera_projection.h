// Camera3D's screen -> world projection (camera_3d.cpp at 4.1.4-stable), which
// MouseRaycast.get_selection uses to build its ray:
//   start = project_ray_origin(mouse); end = project_position(mouse, 20000)
//
// `viewport` is the Viewport's visible rect size. The game's is 960x540
// (project.godot [display] viewport_width/height, stretch mode "viewport").
#pragma once

#include "godot/node.h"

namespace godot {

inline constexpr Vector2 kViewportSize{960.0f, 540.0f};

// camera_3d.cpp:321 (perspective): the camera transform's origin.
Vector3 project_ray_origin(const Camera3D& camera, Vector2 screen, Vector2 viewport);
// camera_3d.cpp:393: the point `z_depth` in front of the camera under `screen`.
Vector3 project_position(const Camera3D& camera, Vector2 screen, float z_depth, Vector2 viewport);

}  // namespace godot
