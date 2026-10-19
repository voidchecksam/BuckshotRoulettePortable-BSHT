// source: scripts/BriefcaseMachine.gd (70 lines)
// PORT deviations:
//   (none)
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

class BriefcaseMachine : public godot::Script {
public:
    static constexpr const char* kClassName = "BriefcaseMachine";
    const char* get_class_name() const override { return kClassName; }

    EndingManager* ending = nullptr;
    CameraManager* cam = nullptr;
    CursorManager* cursor = nullptr;
    godot::Node3D* parent_eyes = nullptr;
    godot::Node3D* parent_machine = nullptr;
    godot::AudioStreamPlayer* speaker_machine = nullptr;
    godot::AnimationPlayer* anim_machine = nullptr;
    godot::AudioStreamPlayer* speaker_latchL = nullptr;
    godot::AudioStreamPlayer* speaker_latchR = nullptr;
    godot::AudioStreamPlayer* speaker_lid = nullptr;
    godot::AnimationPlayer* anim_latchL = nullptr;
    godot::AnimationPlayer* anim_latchR = nullptr;
    godot::AnimationPlayer* anim_lid = nullptr;
    InteractionBranch* intbranch_L = nullptr;
    InteractionBranch* intbranch_R = nullptr;
    InteractionBranch* intbranch_lid = nullptr;
    ControllerManager* controller = nullptr;
    godot::Control* btnParent_briefcase = nullptr;
    godot::Control* btn_left = nullptr;

    bool latchRaisedL = false;   // gd: L23
    bool latchRaisedR = false;   // gd: L24

    seq::Task MainRoutine();                 // gd: L26-L42
    seq::Task CheckLatches();                // gd: L44-L47
    void OpenLatch(const std::string& alias);  // gd: L49-L62
    void OpenLid();                          // gd: L64-L70
};

}  // namespace scripts
