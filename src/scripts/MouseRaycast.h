// source: scripts/MouseRaycast.gd (28 lines)
// PORT deviations:
//   - `result` is the Dictionary intersect_ray returns: std::nullopt stands for
//     the empty Dictionary (no hit), a godot::RayResult for a hit.
//   - get_world_3d().direct_space_state is the SceneTree's physics space, and
//     project_ray_origin / project_position use the Viewport's size (960x540).
#pragma once

#include "godot/camera_projection.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <optional>

namespace scripts {

class MouseRaycast : public godot::Script {
public:
    static constexpr const char* kClassName = "MouseRaycast";
    const char* get_class_name() const override { return kClassName; }

    bool checkingOverride = false;
    CursorManager* cursor = nullptr;
    godot::Vector2 mouse;
    std::optional<godot::RayResult> result;
    bool controller_overriding = false;

    bool has_input() const override { return true; }
    void _input(const godot::InputEventRef& event) override;   // gd: L10-L12
    bool has_process() const override { return true; }
    void _process(double delta) override;                       // gd: L14-L15
    void get_selection();                                       // gd: L17-L21
    void GetRaycastOverride(godot::Vector2 pos_override);       // gd: L23-L25
    void StopRaycastOverride();                                 // gd: L27-L28
};

}  // namespace scripts
