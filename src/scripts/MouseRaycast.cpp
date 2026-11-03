#include "scripts/MouseRaycast.h"

#include "godot/camera_projection.h"
#include "godot/scene_tree.h"
#include "scripts/PickupIndicator.h"

#include <cmath>

namespace scripts {
namespace {

constexpr int kMaxHitsBehind = 8;

// The item a collider belongs to: its parent, when that parent carries a PickupIndicator.
const godot::Node3D* PickupItem(const godot::Node* collider) {
    const godot::Node* parent = collider ? collider->get_parent() : nullptr;
    if (!parent || parent->get_child_count() == 0) return nullptr;
    if (!godot::script_cast<PickupIndicator>(parent->get_child(0))) return nullptr;
    return godot::cast_to<godot::Node3D>(parent);
}

float AimDistance(const godot::Camera3D& camera, const godot::Node3D& item, godot::Vector2 aim) {
    const godot::Vector2 p = godot::unproject_position(camera, item.get_global_position(), godot::kViewportSize);
    return std::hypot(p.x - aim.x, p.y - aim.y);
}

// PORT: a hovered item rises toward the camera (PickupIndicator), and a tall one
// (a beer, the handcuffs) then covers the next bracket's point, so the hover never
// leaves it. A controller's bracket marks the item meant: of the items along the
// ray, take the one whose origin (it stays put while the mesh rises) lands nearest
// the aim.
godot::RayResult ItemUnderAim(const godot::PhysicsDirectSpaceState3D& space, const godot::Camera3D& camera,
                              godot::RayQuery query, const godot::RayResult& nearest, godot::Vector2 aim) {
    const godot::Node3D* item = PickupItem(nearest.collider);
    if (!item) return nearest;
    godot::RayResult best = nearest;
    float bestDistance = AimDistance(camera, *item, aim);
    godot::RayResult hit = nearest;
    for (int i = 0; i < kMaxHitsBehind; ++i) {
        query.exclude.push_back(hit.collider);
        if (!space.intersect_ray(query, hit)) break;
        const godot::Node3D* behind = PickupItem(hit.collider);
        if (!behind) continue;
        const float d = AimDistance(camera, *behind, aim);
        if (d < bestDistance) {
            bestDistance = d;
            best = hit;
        }
    }
    return best;
}

}  // namespace

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
        if (controller_overriding) hit = ItemUnderAim(tree->physics_space(), *camera, query, hit, mouse);
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
