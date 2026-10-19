// source: scripts/GameOverManager.gd (4 lines)
// PORT deviations:
//   - PlayerWon is never called in 1.2 (RoundManager.gd:492 is commented out),
//     so win.tscn is not ported: the request would be refused by the host.
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

class GameOverManager : public godot::Script {
public:
    static constexpr const char* kClassName = "GameOverManager";
    const char* get_class_name() const override { return kClassName; }


    void PlayerWon();   // gd: L3-L4
};

}  // namespace scripts
