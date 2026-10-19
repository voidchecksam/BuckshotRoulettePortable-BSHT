// source: scripts/ShotgunShooting.gd (238 lines)
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

class ShotgunShooting : public godot::Script {
public:
    static constexpr const char* kClassName = "ShotgunShooting";
    const char* get_class_name() const override { return kClassName; }

    ShellEjectManager* ejectionManager_player = nullptr;
    CursorManager* cursorManager = nullptr;
    PickupIndicator* shotgunIndicator = nullptr;
    RoundManager* roundManager = nullptr;
    ShellSpawner* shellSpawner = nullptr;
    godot::StaticBody3D* collider_shotgun = nullptr;
    DecisionTextManager* decisionText = nullptr;
    ObjectShaker* shotgunshaker = nullptr;
    PermissionManager* perm = nullptr;
    godot::AnimationPlayer* animator_shotgun = nullptr;
    CameraManager* camera = nullptr;
    godot::AudioStreamPlayer* speaker_live = nullptr;
    godot::AudioStreamPlayer* speaker_blank = nullptr;
    HealthCounter* healthCounter = nullptr;
    godot::AnimationPlayer* animator_muzzleFlash = nullptr;
    godot::AnimationPlayer* animator_muzzleFlash_model = nullptr;
    CameraShaker* cameraShaker = nullptr;
    DeathManager* death = nullptr;
    SmokeController* smoke = nullptr;
    std::vector<SpeakerController*> speakerArray_pitchShift;
    std::vector<SpeakerController3D*> speakerArray_pitchShift3D;
    TimeScaleManager* timescale = nullptr;
    std::vector<std::string> splatters;
    godot::GeometryInstance3D* mat_splatter = nullptr;
    godot::AudioStreamPlayer* speaker_splatter = nullptr;
    godot::AnimationPlayer* anim_splatter = nullptr;
    Achievement* ach = nullptr;
    godot::Control* btnParent_shootingChoice = nullptr;
    godot::Control* btn_you = nullptr;
    ControllerManager* controller = nullptr;

    bool playerCanGoAgain = false;
    bool disablingDelayShit = false;
    bool dealerShotTrue = false;

    // gd: L168-L170, L217-L220. The names are the original's.
    bool whatTheFuck = false;
    bool delaying = false;
    bool isPlayerSide = false;
    bool fired = false;
    int temphealth_dealer = 0;
    int temphealth_player = 0;
    std::string whoshot;

    seq::Task GrabShotgun();                 // gd: L36-L48
    void MainSlowDownRoutine(std::string whoCopy, bool fromDealer);   // gd: L53-L70
    seq::Task BloodSplatter();                                        // gd: L73-L85
    seq::Task Shoot(std::string who);        // gd: L87-L156
    seq::Task ShootingDealerEjection(std::string currentRoundInChamberCopy,
                                     std::string whoCopy, bool hasDelay);   // gd: L158-L166
    seq::Task FinalizeShooting(bool playerCanGoAgain, bool placeShotgunOnTable,
                               bool waitForRevival, bool addDelay);         // gd: L171-L192
    void CheckAchievement_coinflip();   // gd: L194-L197
    void CheckAchievement_why();        // gd: L199-L200
    void CheckAchievement_style();      // gd: L202-L203
    void PlayShootingSound();                // gd: L205-L215
    void CheckIfFinalShot();                 // gd: L221-L230
    void ShotgunCollider(bool isEnabled);    // gd: L232-L238
};

}  // namespace scripts
