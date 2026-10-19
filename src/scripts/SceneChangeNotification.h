// source: scripts/SceneChangeNotification.gd (4 lines)
// PORT deviations:
//   - get_tree().current_scene is the tree's root here: the port
//     loads one scene, and its root is the scene's.
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

class SceneChangeNotification : public godot::Script {
public:
    static constexpr const char* kClassName = "SceneChangeNotification";
    const char* get_class_name() const override { return kClassName; }


    void _ready() override;
};

}  // namespace scripts
