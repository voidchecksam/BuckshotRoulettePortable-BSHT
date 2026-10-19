#include "scripts/MouseRaycast.h"

#include "godot/scene_tree.h"

namespace scripts {

// gd: L10-L12
void MouseRaycast::_input(const godot::InputEventRef& event) {
    if (const auto* m = godot::event_cast<godot::InputEventMouse>(event.get())) {
        if (!controller_overriding) mouse = m->position;
    }
}

// gd: L14-L15
void MouseRaycast::_process(double /*delta*/) {
    get_selection();
}

// gd: L17-L21
void MouseRaycast::get_selection() {
    const auto* camera = godot::cast_to<godot::Camera3D>(get_node_owner());
    godot::SceneTree* tree = get_tree();
    if (!camera || !tree) return;   // PORT: the script is only ever on the Camera (a Camera3D)
    const godot::Vector3 start = godot::project_ray_origin(*camera, mouse, godot::kViewportSize);
    const godot::Vector3 end = godot::project_position(*camera, mouse, 20000.0f, godot::kViewportSize);
    godot::RayQuery query;   // PhysicsRayQueryParameters3D.create(start, end)
    query.from = start;
    query.to = end;
    godot::RayResult hit;
    if (tree->physics_space().intersect_ray(query, hit)) {
        result = hit;
    } else {
        result.reset();
    }
}

// gd: L23-L25
void MouseRaycast::GetRaycastOverride(godot::Vector2 pos_override) {
    controller_overriding = true;
    mouse = pos_override;
}

// gd: L27-L28
void MouseRaycast::StopRaycastOverride() {
    controller_overriding = false;
}

}  // namespace scripts
