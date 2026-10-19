// source: scripts/StatueManager.gd (10 lines)
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

class Statue : public godot::Script {
public:
    static constexpr const char* kClassName = "Statue";
    const char* get_class_name() const override { return kClassName; }

    RoundManager* rm = nullptr;
    godot::Node3D* statue = nullptr;
    godot::Node3D* cup = nullptr;

    void CheckStatus();
};

}  // namespace scripts
