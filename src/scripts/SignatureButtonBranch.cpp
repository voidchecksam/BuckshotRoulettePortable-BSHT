#include "scripts/SignatureButtonBranch.h"

#include "godot/math_funcs.h"
#include "godot/scene_tree.h"
#include "scripts/InteractionBranch.h"

namespace scripts {

// gd: L13-L15
void SignButton::_ready() {
    parent = godot::cast_to<godot::Node3D>(get_node_owner()->get_parent());
    intbranch = parent ? godot::script_cast<InteractionBranch>(parent->get_child(1)) : nullptr;
}

// gd: L17-L18
void SignButton::_process(double /*delta*/) {
    Lerp();
}

// gd: L20-L34
seq::Task SignButton::Press() {
    if (intbranch) intbranch->interactionAllowed = false;
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
    if (intbranch) intbranch->interactionAllowed = true;
}

// gd: L36-L41
void SignButton::Lerp() {
    if (moving && parent) {
        elapsed += get_process_delta_time();
        const double c = elapsed / dur;
        const double temp_pos = godot::lerpf(cur, next, c);
        const godot::Vector3 o = parent->get_position();
        parent->set_transform_origin({o.x, static_cast<float>(temp_pos), o.z});
    }
}

}  // namespace scripts
