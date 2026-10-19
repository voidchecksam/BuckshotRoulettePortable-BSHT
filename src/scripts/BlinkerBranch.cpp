#include "scripts/BlinkerBranch.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"

namespace scripts {

// gd: L10-L14
void BlinkerBranch::_ready() {
    parent = godot::cast_to<godot::MeshInstance3D>(get_node_owner()->get_parent());
    looping = true;
    get_tree()->spawn(Loop());   // called without await in the .gd
}

// gd: L16-L23
seq::Task BlinkerBranch::Loop() {
    while (looping) {
        const double delay = godot::randf_range(delay_range1, delay_range2);
        parent->set_surface_override_material(0, mat_1);
        co_await get_tree()->create_timer(delay);
        parent->set_surface_override_material(0, mat_2);
        co_await get_tree()->create_timer(delay);
    }
}

}  // namespace scripts
