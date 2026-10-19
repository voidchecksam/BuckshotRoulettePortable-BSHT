// source: scripts/HeavenManager.gd (84 lines)
// PORT deviations:
//   - get_tree().quit() and change_scene_to_file are performed by the host.
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

class Heaven : public godot::Script {
public:
    static constexpr const char* kClassName = "Heaven";
    const char* get_class_name() const override { return kClassName; }

    ControllerManager* controller = nullptr;
    CursorManager* cursor = nullptr;
    godot::Control* viewblocker = nullptr;
    godot::AudioStreamPlayer* speaker_music = nullptr;
    godot::AnimationPlayer* animator = nullptr;
    InteractionBranch* intbranch_heavendoor = nullptr;
    InteractionManager* interactionManager = nullptr;
    std::vector<ButtonClass*> buttonArray;
    godot::AudioStreamPlayer* speaker_button = nullptr;
    std::vector<godot::Control*> ui;
    Achievement* ach = nullptr;
    godot::Node3D* bracket_door = nullptr;
    godot::Control* btn_door = nullptr;
    godot::Control* btn_retry = nullptr;
    godot::Control* btn_exit = nullptr;

    void _ready() override;                 // gd: L15-L20
    void Signals();                         // gd: L22-L25
    void CheckControllerState();            // gd: L27-L29
    void Button_Retry();                    // gd: L31-L32
    void Button_Exit();                     // gd: L34-L35
    seq::Task Button_Main(std::string tag); // gd: L37-L51
    seq::Task BeginLoop();                  // gd: L57-L68
    seq::Task Fly();                        // gd: L70-L84

private:
    seq::Task ReadyRoutine();
};

}  // namespace scripts
