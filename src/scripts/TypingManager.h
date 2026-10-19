// source: scripts/TypingManager.gd (44 lines)
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

class TypingManager : public godot::Script {
public:
    static constexpr const char* kClassName = "TypingManager";
    const char* get_class_name() const override { return kClassName; }

    godot::AudioStreamPlayer* speaker_click = nullptr;
    godot::Label3D* dialogueUI = nullptr;
    double dialogueSpeed = 0.0;
    double incrementDelay = 0.0;

    // PORT: omitted ShowText_ForDuration (nothing calls it; RoundManager only holds the node), gd: L12-L21
    // PORT: omitted ShowText_Forever (nothing calls it), gd: L23-L29
    // PORT: omitted HideText (nothing calls it), gd: L31-L34
    // PORT: omitted TickText (nothing calls it), gd: L36-L44
};

}  // namespace scripts
