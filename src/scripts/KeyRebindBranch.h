// source: scripts/KeyRebindBranch.gd (3 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/input_event.h"
#include "godot/script.h"
#include "scripts/forward.h"

namespace scripts {

class RebindBranch : public godot::Script {
public:
    static constexpr const char* kClassName = "RebindBranch";
    const char* get_class_name() const override { return kClassName; }

    godot::InputEventRef originalBind_keyboard;
    godot::InputEventRef originalBind_controller;
};

}  // namespace scripts
