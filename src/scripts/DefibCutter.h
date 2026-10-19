// source: scripts/DefibCutter.gd (161 lines)
// PORT deviations: none.
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

class DefibCutter : public godot::Script {
public:
    static constexpr const char* kClassName = "DefibCutter";
    const char* get_class_name() const override { return kClassName; }

    CameraManager* cam = nullptr;
    Dialogue* dia = nullptr;
    godot::AnimationPlayer* animator_main = nullptr;
    std::vector<godot::Node3D*> consoleArray;
    godot::AudioStreamPlayer* speaker_motor = nullptr;
    godot::AudioStreamPlayer* speaker_bootup = nullptr;
    godot::AudioStreamPlayer* speaker_beep = nullptr;
    godot::AudioStreamPlayer* speaker_blademove = nullptr;
    DefibBlade* blade_player = nullptr;
    DefibBlade* blade_dealer = nullptr;
    RoundManager* roundManager = nullptr;
    godot::Node3D* errorUI_player = nullptr;
    godot::Node3D* errorUI_dealer = nullptr;
    godot::AudioStreamPlayer* speaker_error = nullptr;
    std::vector<std::string> soundArray_error;
    godot::Node3D* healthUI_player = nullptr;
    godot::Node3D* healthUI_dealer = nullptr;
    godot::Node3D* healthUI_divider = nullptr;

    bool cutDialogueSent = false;        // gd: L22
    bool displayBroken_player = false;   // gd: L131
    bool displayBroken_dealer = false;   // gd: L132

    seq::Task InitialSetup();                  // gd: L24-L50
    seq::Task CutWire(std::string who);        // gd: L52-L77
    seq::Task FlashError(std::string who);     // gd: L80-L98
    seq::Task BlipError(std::string who);      // gd: L100-L113
    seq::Task BlipError_Both();                // gd: L115-L129
    void BreakDisplay(std::string who);        // gd: L133-L141
    void PlayError();                          // gd: L143-L147
};

}  // namespace scripts
