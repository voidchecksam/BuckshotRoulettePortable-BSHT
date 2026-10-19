// source: scripts/ItemGridIndicator.gd (12 lines)
// PORT deviations:
//   - ShowGridIndicator awaits a timer: the await lives in HideRoutine,
//     spawned owned by this node; the caller does not await (nor does gd's).
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

class GridIndicator : public godot::Script {
public:
    static constexpr const char* kClassName = "GridIndicator";
    const char* get_class_name() const override { return kClassName; }

    RoundManager* roundManager = nullptr;
    godot::AnimationPlayer* animator = nullptr;
    godot::Node3D* parent = nullptr;

    void ShowGridIndicator();                        // gd: L7-L12
    seq::Task HideRoutine();                         // PORT: the awaiting half
};

}  // namespace scripts
