#include "scripts/ObjectShaker.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"

namespace scripts {

// gd: L11-L14
void ObjectShaker::StartShaking() {
    originalPos = obj->get_position();
    shaking = true;
    get_tree()->spawn(ShakeRoutine());   // called without await in the .gd
}

// gd: L16-L18
void ObjectShaker::StopShaking() {
    shaking = false;
    obj->set_transform_origin(originalPos);
}

// gd: L20-L28
seq::Task ObjectShaker::ShakeRoutine() {
    while (shaking) {
        const float val = static_cast<float>(godot::randf_range(offset_1, offset_2));
        const godot::Vector3 o = obj->get_position();
        obj->set_transform_origin({o.x + val, o.y + val, o.z + val});
        co_await get_tree()->create_timer(delay);
        obj->set_transform_origin(originalPos);
        co_await get_tree()->create_timer(delay);
    }
}

}  // namespace scripts
