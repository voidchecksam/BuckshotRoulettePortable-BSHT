// source: scripts/BpmLight.gd (26 lines)
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

class BpmLight : public godot::Script {
public:
    static constexpr const char* kClassName = "BpmLight";
    const char* get_class_name() const override { return kClassName; }

    godot::AnimationPlayer* animator = nullptr;

    double delay = 0.0;          // gd: an untyped `var`; IntroManager.MainTrackLoad sets it
    godot::Node* l = nullptr;
    bool looping = false;

    void _ready() override;      // gd: L8-L9
    void BeginMainLoop();        // gd: L11-L13
    void Hit();                  // gd: L15-L17
    seq::Task BeginLoop();       // gd: L19-L26
};

}  // namespace scripts
