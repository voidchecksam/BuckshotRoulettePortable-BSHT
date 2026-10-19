#include "scripts/BpmLight.h"

#include "godot/scene_tree.h"

namespace scripts {

// gd: L8-L9
void BpmLight::_ready() {
    l = get_node_owner()->get_parent();
}

// gd: L11-L13
void BpmLight::BeginMainLoop() {
    looping = true;
    get_tree()->spawn(BeginLoop(), get_node_owner());   // called without await in the .gd
}

// gd: L15-L17
void BpmLight::Hit() {
    if (animator) animator->play("hit");
}

// gd: L19-L26
seq::Task BpmLight::BeginLoop() {
    while (looping) {
        Hit();
        co_await get_tree()->create_timer(delay);
        Hit();
        co_await get_tree()->create_timer(delay);
    }
}

}  // namespace scripts
