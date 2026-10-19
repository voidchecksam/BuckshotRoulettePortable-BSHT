// source: scripts/PartitionBranch.gd (42 lines)
// PORT deviations:
//   - LoopPartitions is a coroutine Loop starts without awaiting: spawned,
//     owned by this node.
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

class PartitionBranch : public godot::Script {
public:
    static constexpr const char* kClassName = "PartitionBranch";
    const char* get_class_name() const override { return kClassName; }

    godot::Label3D* p = nullptr;
    double d = .1;
    godot::AudioStreamPlayer* sp = nullptr;

    std::vector<int> nums{9, 9, 9, 9, 9, 9, 9, 9};
    bool finished = false;

    void _ready() override;                          // gd: L10-L13
    void Loop(bool s);                               // gd: L15-L18

    bool looping = false;
    seq::Task LoopPartitions();                      // gd: L23-L36
    void ResetPartition();                           // gd: L38-L42
};

}  // namespace scripts
