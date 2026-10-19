// source: scripts/TextFire.gd (40 lines)
// PORT deviations:
//   (none yet)
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class TextFire : public godot::Script {
public:
    static constexpr const char* kClassName = "TextFire";
    const char* get_class_name() const override { return kClassName; }

    godot::Control* ui = nullptr;                // gd: L3 (not exported)
    godot::AudioStreamPlayer* speaker = nullptr;  // gd: L4 (not exported)
    double max_x = 0.0;
    double max_y = 0.0;
    double duration = 0.0;

    // gd: L9-L13
    double orig_x = 0.0;
    double orig_y = 0.0;
    bool moving = false;
    double elapsed = 0;
    bool changed = false;

    void _ready() override;                   // gd: L16-L18
    void Fire();                              // gd: L20-L27
    bool has_process() const override { return true; }
    void _process(double delta) override;     // gd: L29-L30
    void Lerp();                              // gd: L32-L40
};

}  // namespace scripts
