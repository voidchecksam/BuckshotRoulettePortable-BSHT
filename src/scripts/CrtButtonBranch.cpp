#include "scripts/CrtButtonBranch.h"

#include "godot/math_funcs.h"
#include "godot/scene_tree.h"
#include "scripts/InteractionBranch.h"

namespace scripts {

// gd: L13-L15
void CrtButton::_ready() {
    parent = godot::cast_to<godot::Node3D>(get_node_owner()->get_parent());
    intbranch = godot::script_cast<InteractionBranch>(parent->get_child(0));
}

// gd: L17-L18
void CrtButton::_process(double /*delta*/) {
    Lerp();
}

// gd: L20-L34
seq::Task CrtButton::Press() {
    intbranch->interactionAllowed = false;
    cur = y_up;
    next = y_down;
    elapsed = 0;
    moving = true;
    co_await get_tree()->create_timer(.06);
    moving = false;
    cur = y_down;
    next = y_up;
    elapsed = 0;
    moving = true;
    co_await get_tree()->create_timer(.06);
    moving = false;
    intbranch->interactionAllowed = true;
}

// gd: L36-L41
void CrtButton::Lerp() {
    if (moving) {
        elapsed += get_process_delta_time();
        const double c = elapsed / dur;
        const double temp_pos = godot::lerpf(cur, next, c);
        const core::Mat4& t = parent->get_transform();
        parent->set_transform_origin(godot::Vector3(t.Tx(), static_cast<float>(temp_pos), t.Tz()));
    }
}

}  // namespace scripts
