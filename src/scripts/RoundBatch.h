// source: scripts/RoundBatch.gd (4 lines)
// PORT deviations:
//   (none yet)
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class RoundBatch : public godot::Script {
public:
    static constexpr const char* kClassName = "RoundBatch";
    const char* get_class_name() const override { return kClassName; }

    int batchIndex = 0;
    std::vector<RoundClass*> roundArray;

};

}  // namespace scripts
