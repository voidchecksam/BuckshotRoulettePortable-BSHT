#include "scripts/JankPreventer.h"

#include "godot/control.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"

namespace scripts {

// gd: L6-L8
void JankPreventer::_ready() {
    get_tree()->spawn(Ready(), get_node_owner());
}

seq::Task JankPreventer::Ready() {
    co_await get_tree()->create_timer(1);
    co_await Prevent();
}

// gd: L10-L19
seq::Task JankPreventer::Prevent() {
    const std::string loc = godot::get_locale();
    godot::Node* parent = get_node_owner()->get_parent();
    if (!isHeaven) {
        co_await get_tree()->create_timer(1);
        if (loc == "ES" || loc == "JA") {
            if (auto* n3 = godot::cast_to<godot::Node3D>(parent)) n3->set_visible(false);
            if (auto* c = godot::cast_to<godot::Control>(parent)) c->set_visible(false);
        }
        if (loc == "JA" && text_dealer) text_dealer->set_scale({1.409f, 2.466f, 1.752f});
    } else {
        // gd: the per-locale bracket widths are commented out in v2.2.
        if (auto* label = godot::cast_to<godot::Label>(parent)) label->set_text("");
    }
}

}  // namespace scripts
