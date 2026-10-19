// source: scripts/ShellLoader.gd (99 lines)
// PORT deviations:
//   - `var stringshow` is null in GDScript when diaindex is 2; it is an empty
//     std::string here. Unreachable while numberOfDialogueRead < 3 caps the
//     readings at two (the scene's PlayerData starts it at 1).
//   - ShowText_ForDuration is a coroutine called without await: spawned.
#pragma once

#include "godot/animation_player.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class ShellLoader : public godot::Script {
public:
    static constexpr const char* kClassName = "ShellLoader";
    const char* get_class_name() const override { return kClassName; }

    PermissionManager* perm = nullptr;
    CameraManager* camera = nullptr;
    RoundManager* roundManager = nullptr;
    godot::AudioStreamPlayer* speaker_loadShell = nullptr;
    godot::AudioStreamPlayer* speaker_rackShotgun = nullptr;
    godot::AnimationPlayer* animator_shotgun = nullptr;
    Dialogue* dialogue = nullptr;
    CursorManager* cursor = nullptr;
    PlayerData* playerData = nullptr;
    godot::Node3D* shotgunHand_L = nullptr;
    godot::Node3D* shotgunHand_R = nullptr;
    godot::AnimationPlayer* animator_dealerHands = nullptr;
    godot::AnimationPlayer* animator_dealerHandRight = nullptr;
    DealerIntelligence* dealerAI = nullptr;
    std::vector<std::string> introductionDialogues;
    std::vector<std::string> loadingDialogues;
    int diaindex = 0;

    seq::Task DealerHandsGrabShotgun();               // gd: L21-L26
    void DealerHandsDropShotgun();                    // gd: L28-L32
    seq::Task LoadShells();                           // gd: L34-L99
};

}  // namespace scripts
