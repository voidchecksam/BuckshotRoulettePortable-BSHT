#include "scripts/CrtIcon.h"

namespace scripts {

// gd: L9-L11
void CrtIcon::_ready() {
    instance = godot::cast_to<godot::GeometryInstance3D>(get_node_owner()->get_parent());
    SetState(false);
}

// gd: L14-L16
void CrtIcon::CheckState(int currentIndex) {
    if (currentIndex == activeIndex) SetState(true);
    else SetState(false);
}

// gd: L18-L22
void CrtIcon::SetState(bool act) {
    if (act) {
        instance->set_override_albedo_texture(texture_active);
    } else {
        instance->set_override_albedo_texture(texture_inactive);
    }
}

}  // namespace scripts
