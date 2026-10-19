// source: scripts/JankPreventer.gd (16 lines)
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

class JankPreventer : public godot::Script {
public:
    static constexpr const char* kClassName = "JankPreventer";
    const char* get_class_name() const override { return kClassName; }

    // v2.2
    godot::Label3D* text_dealer = nullptr;

    bool isHeaven = false;

    void _ready() override;   // gd: L6-L8
    seq::Task Ready();        // gd: L6-L8 (the awaiting body of _ready)
    seq::Task Prevent();      // gd: L10-L19
};

}  // namespace scripts
