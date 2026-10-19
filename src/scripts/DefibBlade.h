// source: scripts/DefibBlade.gd (17 lines)
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

class DefibBlade : public godot::Script {
public:
    static constexpr const char* kClassName = "DefibBlade";
    const char* get_class_name() const override { return kClassName; }

    godot::AnimationPlayer* anim = nullptr;
    godot::AudioStreamPlayer* speaker = nullptr;
    std::string sound_open;
    std::string sound_close;
    godot::Node3D* wire_normal = nullptr;
    godot::Node3D* wire_broken = nullptr;

    void Blade(std::string state, bool playSound);   // gd: L10-L17
};

}  // namespace scripts
