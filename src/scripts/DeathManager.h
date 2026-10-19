// source: scripts/DeathManager.gd (174 lines)
// PORT deviations:
//   - Kill's `trueDeath` branches are empty `pass` in the .gd for both sides;
//     they stay empty here.
//   - SaveGame writes main_12; the host also carries PlayerData across the
//     scene change in memory, which is what LoadGame restores.
//   - scene_death (a PackedScene) is not bound: the scene is changed by path.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

// The revival ceremony. Kill is the only thing ShotgunShooting.Shoot awaits,
// and on the dealer's side Kill itself spawns ShootingDealerEjection while
// Shoot is still suspended: two coroutines live at once, which is why the
// turn ends from whichever reaches EndTurn first.
class DeathManager : public godot::Script {
public:
    static constexpr const char* kClassName = "DeathManager";
    const char* get_class_name() const override { return kClassName; }

    CameraManager* cam = nullptr;
    godot::ColorRect* viewblocker = nullptr;
    std::vector<SpeakerController*> speakersToDisable;
    godot::AnimationPlayer* animator_playerDefib = nullptr;
    godot::Node3D* defibParent = nullptr;
    godot::AudioStreamPlayer* speaker_playerDefib = nullptr;
    CameraShaker* cameraShaker = nullptr;
    HealthCounter* healthCounter = nullptr;
    godot::AnimationPlayer* animator_shotgun = nullptr;
    ShellEjectManager* ejectManager_player = nullptr;
    ShellEjectManager* ejectManager_dealer = nullptr;
    ShotgunShooting* shotgunShooting = nullptr;
    godot::AnimationPlayer* animator_dealer = nullptr;
    godot::AudioStreamPlayer3D* speaker_crash = nullptr;
    DealerIntelligence* dealerAI = nullptr;
    godot::AnimationPlayer* animator_dealerHands = nullptr;
    ShellLoader* shellLoader = nullptr;
    SaveFileManager* savefile = nullptr;
    Achievement* ach = nullptr;
    FilterController* filter = nullptr;
    godot::AnimationPlayer* animator_pp = nullptr;
    godot::AudioStreamPlayer* speaker_heartbeat = nullptr;
    RoundManager* rm = nullptr;

    bool shitIsFuckedUp = false;

    void _ready() override;                                              // gd: L27-L28
    seq::Task Kill(std::string who, bool trueDeath, bool returningShotgun);  // gd: L31-L112
    seq::Task MedicineDeath();                                           // gd: L114-L136
    seq::Task MainDeathRoutine();                                        // gd: L139-L160
    void DisableSpeakers();                                              // gd: L162-L164
    void FadeInSpeakers();                                               // gd: L166-L169
};

}  // namespace scripts
