// source: scripts/DialogueManager_Ending.gd (40 lines)
// PORT deviations:
//   - TickText runs as a sequence owned by this node (GDScript's function
//     state dies with the instance).
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

class DialogueEnding : public godot::Script {
public:
    static constexpr const char* kClassName = "DialogueEnding";
    const char* get_class_name() const override { return kClassName; }

    godot::AudioStreamPlayer* speaker_click = nullptr;
    std::vector<std::string> soundArray_clicks;
    godot::Label* dialogueUI = nullptr;
    double dialogueSpeed = 0.0;
    double incrementDelay = 0.0;

    // gd: L9-L11, L29
    double elapsed = 0;
    bool moving = false;
    bool looping = false;
    bool overriding = false;

    void _ready() override;                           // gd: L13-L14
    void ShowText_Forever(std::string activeText);    // gd: L16-L22
    void HideText();                                  // gd: L24-L27
    seq::Task TickText();                             // gd: L30-L40
};

}  // namespace scripts
