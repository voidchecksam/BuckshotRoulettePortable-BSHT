#include "scripts/CameraShaker.h"

#include "godot/rng.h"

namespace scripts {

// gd: L11-L14
void CameraShaker::_ready() {
    if (camera_to_shake) {
        initial_h_offset = camera_to_shake->h_offset;
        initial_v_offset = camera_to_shake->v_offset;
    }
}

// gd: L16-L18
void CameraShaker::Shake() {
    if (camera_to_shake) shake_timer = duration;
}

// gd: L20-L31
void CameraShaker::_process(double delta) {
    if (shake_timer > 0) {
        if (camera_to_shake) {
            const double h_offset = godot::randf() * intensity - intensity / 2.0;
            const double v_offset = godot::randf() * intensity - intensity / 2.0;
            camera_to_shake->h_offset = static_cast<float>(initial_h_offset + h_offset);
            camera_to_shake->v_offset = static_cast<float>(initial_v_offset + v_offset);
            shake_timer -= delta;
        }
    } else {
        if (camera_to_shake) {
            camera_to_shake->h_offset = static_cast<float>(initial_h_offset);
            camera_to_shake->v_offset = static_cast<float>(initial_v_offset);
        }
    }
}

}  // namespace scripts
