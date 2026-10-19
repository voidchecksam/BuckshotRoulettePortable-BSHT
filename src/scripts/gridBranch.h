// source: scripts/gridBranch.gd (2 lines)
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

class gridBranch : public godot::Script {
public:
    static constexpr const char* kClassName = "gridBranch";
    const char* get_class_name() const override { return kClassName; }

    int activeIndex = 0;

};

}  // namespace scripts
