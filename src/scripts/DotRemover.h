// source: scripts/DotRemover.gd (17 lines)
// PORT deviations:
//   - `_ready` awaits a timer: the await lives in ReadyRoutine, spawned from
//     _ready and owned by this node.
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

class DotFix : public godot::Script {
public:
    static constexpr const char* kClassName = "DotFix";
    const char* get_class_name() const override { return kClassName; }

    std::string key;
    godot::Label3D* p = nullptr;

    void _ready() override;                          // gd: L6-L10
    seq::Task ReadyRoutine();                        // PORT: the awaiting half of _ready
    void Fix();                                      // gd: L12-L17
};

}  // namespace scripts
