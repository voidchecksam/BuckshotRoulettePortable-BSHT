// source: scripts/HandcuffManager.gd (56 lines)
// PORT deviations:
//   - call() dispatches the METHOD keys of `dealer use handcuffs`
//     (AttachHandCuffs, CheckPlayerHandCuffs_Animation); each is spawned, as
//     a method track's call runs to the first await and returns.
//   - `var previousSocket` (untyped) holds CameraManager.activeSocket: string.
#pragma once

#include "godot/animation_player.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

namespace scripts {

class HandcuffManager : public godot::Script {
public:
    static constexpr const char* kClassName = "HandcuffManager";
    const char* get_class_name() const override { return kClassName; }

    DealerIntelligence* dealerAI = nullptr;
    RoundManager* roundManager = nullptr;
    CameraManager* camera = nullptr;
    godot::AnimationPlayer* animator_handcuffsPlayer = nullptr;
    godot::Node3D* handcuffs_player = nullptr;

    std::string previousSocket;                      // gd: L9

    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    seq::Task AttachHandCuffs();                               // gd: L11-L14
    seq::Task CheckPlayerHandCuffs(bool lerpingToPrevious);    // gd: L16-L22
    seq::Task BreakPlayerHandCuffs(bool lerpingToPrevious);    // gd: L24-L30
    seq::Task CheckPlayerHandCuffs_Animation();                // gd: L32-L38
    seq::Task RemoveAllCuffsRoutine();               // gd: L40-L56
};

}  // namespace scripts
