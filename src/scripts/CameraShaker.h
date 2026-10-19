// source: scripts/CameraShaker.gd (31 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/node.h"
#include "godot/script.h"

namespace scripts {

// Shakes the VIEW, not the node: it writes Camera3D's h_offset / v_offset,
// which camera_3d.cpp:166-171 applies to the camera transform while leaving the
// node's global transform alone. One randf() per frame while a shake runs, so
// it is a live consumer of the global RNG stream.
class CameraShaker : public godot::Script {
public:
    static constexpr const char* kClassName = "CameraShaker";
    const char* get_class_name() const override { return kClassName; }

    godot::Camera3D* camera_to_shake = nullptr;
    double intensity = 0.0;
    double duration = 0.0;

    double shake_timer = 0.0;
    double initial_h_offset = 0.0;
    double initial_v_offset = 0.0;

    bool has_process() const override { return true; }   // the .gd defines _process
    void _ready() override;                 // gd: L11-L14
    void Shake();                           // gd: L16-L18
    void _process(double delta) override;   // gd: L20-L31
};

}  // namespace scripts
