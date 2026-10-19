// source: scripts/BlockoutManager.gd (7 lines)
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

class Blockout : public godot::Script {
public:
    static constexpr const char* kClassName = "Blockout";
    const char* get_class_name() const override { return kClassName; }

    std::vector<godot::Node3D*> objArray_club;

    void HideClub();   // gd: L5-L7
};

}  // namespace scripts
