// source: scripts/SignatureButtonBranch.gd (41 lines)
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

class SignButton : public godot::Script {
public:
    static constexpr const char* kClassName = "SignButton";
    const char* get_class_name() const override { return kClassName; }

    double y_up = 0.0;
    double y_down = 0.0;
    double dur = 0.0;

    godot::Node3D* parent = nullptr;
    InteractionBranch* intbranch = nullptr;
    double elapsed = 0;
    bool moving = false;
    double cur = 0;              // gd: untyped `var cur` / `var next`, floats here
    double next = 0;

    void _ready() override;                          // gd: L13-L15
    bool has_process() const override { return true; }
    void _process(double delta) override;            // gd: L17-L18
    seq::Task Press();                               // gd: L20-L34
    void Lerp();                                     // gd: L36-L41
};

}  // namespace scripts
