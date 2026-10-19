// source: scripts/DecisionTextManager.gd (31 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <vector>

namespace scripts {

class DecisionTextManager : public godot::Script {
public:
    static constexpr const char* kClassName = "DecisionTextManager";
    const char* get_class_name() const override { return kClassName; }

    // v2.2
    InteractionManager* inter = nullptr;
    std::vector<godot::Node3D*> array_obj;

    std::vector<TextInteraction*> textArray;
    std::vector<godot::StaticBody3D*> colliderArray;
    godot::AnimationPlayer* animator = nullptr;
    godot::Control* uiParent = nullptr;

    void _ready() override;     // gd: L10-L14
    void SetUI(bool state);     // gd: L16-L31
};

}  // namespace scripts
