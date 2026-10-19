#include "scripts/ShotgunShooting.h"

#include <algorithm>

#include "godot/rng.h"
#include "scripts/AchievementManager.h"
#include "godot/scene_tree.h"
#include "scripts/CameraManager.h"
#include "scripts/CameraShaker.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/DeathManager.h"
#include "scripts/DecisionTextManager.h"
#include "scripts/HealthCounter.h"
#include "scripts/MusicManager.h"
#include "scripts/ObjectShaker.h"
#include "scripts/PermissionManager.h"
#include "scripts/PickupIndicator.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/SmokeController.h"
#include "scripts/SpeakerController.h"
#include "scripts/SpeakerController3D.h"
#include "scripts/TimeScaleManager.h"
#include "scripts/resources.h"

namespace scripts {

// gd: L36-L48
seq::Task ShotgunShooting::GrabShotgun() {
    roundManager->ClearDeskUI(true);
    perm->SetIndicators(false);
    perm->SetInteractionPermissions(false);
    perm->RevertDescriptionUI();
    ShotgunCollider(false);
    animator_shotgun->play("player grab shotgun");
    shotgunshaker->StartShaking();
    decisionText->SetUI(true);
    btnParent_shootingChoice->set_visible(true);
    if (cursorManager->controller_active) btn_you->grab_focus();
    controller->previousFocus = btn_you;
    co_await get_tree()->create_timer(.5);
}

// gd: L53-L70. Not a coroutine in the .gd (no await), and Shoot calls it
// without await, so it runs to completion inline.
void ShotgunShooting::MainSlowDownRoutine(std::string whoCopy, bool fromDealer) {
    const std::string who = whoCopy;
    const std::string currentRoundInChamber = shellSpawner->sequenceArray[0];
    int healthAfterShot = 1;
    if (!fromDealer) {
        healthAfterShot = roundManager->health_opponent - roundManager->currentShotgunDamage;
        if (healthAfterShot < 0) healthAfterShot = 0;
    } else {
        if (roundManager->health_opponent == 0) healthAfterShot = 0;
    }
    if (currentRoundInChamber == "live" && who == "dealer" && healthAfterShot == 0) {
        // TIME SCALE SHIFT
        for (std::size_t i = 0; i < speakerArray_pitchShift.size(); i++) {
            speakerArray_pitchShift[i]->fadeDuration_pitch = timescale->pitchShiftDuration;
            speakerArray_pitchShift[i]->BeginPitchShift(speakerArray_pitchShift[i]->lowestPitchValue,
                                                        speakerArray_pitchShift[i]->originalPitch);
        }
        for (std::size_t i = 0; i < speakerArray_pitchShift3D.size(); i++) {
            speakerArray_pitchShift3D[i]->fadeDuration_pitch = timescale->pitchShiftDuration;
            speakerArray_pitchShift3D[i]->BeginPitchShift(speakerArray_pitchShift3D[i]->lowestPitchValue,
                                                          speakerArray_pitchShift3D[i]->originalPitch);
        }
        timescale->BeginTimeScaleLerp(.2, 1.0);
        get_tree()->spawn(BloodSplatter());   // called without await in the .gd
    }
}

// gd: L73-L85
seq::Task ShotgunShooting::BloodSplatter() {
    co_await get_tree()->create_timer(.17);
    speaker_splatter->set_pitch_scale(static_cast<float>(godot::randf_range(.3, .5)));
    speaker_splatter->play();
    anim_splatter->play("fade out");
    // splatters[] hold the baker's texture keys (TextureKeyForResource); each
    // swap selects the plane's texture-keyed variant page.
    const auto splat = [this](std::size_t i) {
        if (i < splatters.size()) mat_splatter->set_override_albedo_texture(splatters[i]);
    };
    splat(0);
    co_await get_tree()->create_timer(.08);
    splat(1);
    co_await get_tree()->create_timer(.08);
    splat(2);
    co_await get_tree()->create_timer(.08);
    splat(3);
}

// gd: L87-L156. `who` is taken BY VALUE: coroutine parameters are copied into
// the frame, captures are not.
seq::Task ShotgunShooting::Shoot(std::string who) {
    dealerShotTrue = false;
    disablingDelayShit = false;
    playerCanGoAgain = false;
    ejectionManager_player->FadeOutShell();
    cursorManager->SetCursor(false, false);

    // gd: L95-L106, #CAMERA CONTROLS & ANIMATION DEPENDING ON WHO IS SHOT
    if (who == "self") {
        co_await get_tree()->create_timer(.25);
        animator_shotgun->play("player shoot self");
        co_await get_tree()->create_timer(.5);
        camera->BeginLerp("enemy");
    } else if (who == "dealer") {
        co_await get_tree()->create_timer(.25);
        animator_shotgun->play("player shoot dealer");
        co_await get_tree()->create_timer(.5);
        camera->BeginLerp("enemy");
    }

    // PLAY CORRECT SOUND. ASSIGN CURRENT ROUND IN CHAMBER (gd: L107-L112).
    co_await get_tree()->create_timer(2);
    const std::string currentRoundInChamber = shellSpawner->sequenceArray[0];
    MainSlowDownRoutine(who, false);   // called without await in the .gd
    if (who == "self") {
        whoshot = "player";
    } else {
        whoshot = "dealer";
    }
    PlayShootingSound();

    // SHAKE CAMERA (gd: L114-L116).
    if (currentRoundInChamber == "live") {
        smoke->SpawnSmoke("barrel");   // PORT: draws nothing yet (SmokeController)
        cameraShaker->Shake();
    }

    // SUBTRACT HEALTH. ASSIGN PLAYER CAN GO AGAIN. RETURN IF DEAD (gd: L118-L145).
    bool playerDied = false;
    bool dealerShot = false;
    if (currentRoundInChamber == "live" && who == "dealer") {
        roundManager->health_opponent -= roundManager->currentShotgunDamage;
        if (roundManager->health_opponent < 0) roundManager->health_opponent = 0;
    }
    if (currentRoundInChamber == "live" && who == "self") {
        CheckAchievement_why();
        CheckAchievement_style();
        roundManager->waitingForHealthCheck2 = true;
        if (shellSpawner->sequenceArray.size() == 1) whatTheFuck = true;
        roundManager->waitingForDealerReturn = true;
        healthCounter->playerShotSelf = true;
        playerDied = true;
        roundManager->health_player -= roundManager->currentShotgunDamage;
        if (roundManager->health_player < 0) roundManager->health_player = 0;
        playerCanGoAgain = false;
        healthCounter->checkingPlayer = true;
        co_await death->Kill("player", false, true);
    }
    if (currentRoundInChamber == "blank" && who == "self") {
        playerCanGoAgain = true;
        CheckAchievement_coinflip();
    }
    if (currentRoundInChamber == "live" && who == "dealer") {
        playerCanGoAgain = false;
        dealerShot = true;
        dealerShotTrue = true;
        co_await death->Kill("dealer", false, false);
    }
    if (playerDied) co_return;
    if (dealerShot) co_return;
    co_await get_tree()->create_timer(.3);

    // EJECTING SHELLS (gd: L147-L156).
    if (currentRoundInChamber == "blank" && who == "self") {
        animator_shotgun->play("player eject shell");
        co_await get_tree()->create_timer(.85);
        camera->BeginLerp("home");
        co_await get_tree()->create_timer(1.35);
        get_tree()->spawn(FinalizeShooting(playerCanGoAgain, true, false, false));
        co_return;
    }
    if (currentRoundInChamber == "blank" && who == "dealer") disablingDelayShit = true;
    get_tree()->spawn(ShootingDealerEjection(currentRoundInChamber, who, false));
}

// gd: L158-L166
seq::Task ShotgunShooting::ShootingDealerEjection(std::string currentRoundInChamberCopy,
                                                  std::string whoCopy, bool hasDelay) {
    if ((currentRoundInChamberCopy == "blank" || currentRoundInChamberCopy == "live") &&
        whoCopy == "dealer") {
        animator_shotgun->play("player eject shell2");
        co_await get_tree()->create_timer(2);
    }
    if (hasDelay) co_await get_tree()->create_timer(2.5);
    if (!disablingDelayShit) {
        get_tree()->spawn(FinalizeShooting(playerCanGoAgain, true, true, false));
    } else {
        get_tree()->spawn(FinalizeShooting(playerCanGoAgain, true, false, false));
    }
}

// gd: L171-L192. `playerCanGoAgain` shadows the member in the .gd too: the
// parameter is what this reads, and it is taken BY VALUE like every other
// coroutine parameter here.
seq::Task ShotgunShooting::FinalizeShooting(bool playerCanGoAgain, bool placeShotgunOnTable,
                                            bool waitForRevival, bool addDelay) {
    if (placeShotgunOnTable) animator_shotgun->play("player place on table after eject");
    shotgunshaker->StopShaking();
    // gd: the sequenceArray.remove_at(0) here is commented out; the ejection owns it.
    co_await get_tree()->create_timer(.6);
    shotgunIndicator->Revert();
    ShotgunCollider(true);
    if (death->shitIsFuckedUp) co_await get_tree()->create_timer(3);   // gd: L178
    if (waitForRevival) co_await get_tree()->create_timer(2);
    if (addDelay) co_await get_tree()->create_timer(3);
    if (delaying) co_await get_tree()->create_timer(4);
    if (whatTheFuck) co_await get_tree()->create_timer(2);
    whatTheFuck = false;
    if (roundManager->shellSpawner->sequenceArray.empty() && dealerShotTrue) {
        co_await get_tree()->create_timer(2);
    }
    if ((roundManager->health_opponent <= 2 || roundManager->health_player <= 2) &&
        roundManager->playerData->currentBatchIndex == 2 && !roundManager->endless) {
        if (roundManager->health_opponent <= 2 && !roundManager->wireIsCut_dealer) {
            co_await get_tree()->create_timer(2);
        }
        if (roundManager->health_player <= 2 && !roundManager->wireIsCut_player) {
            co_await get_tree()->create_timer(2);
        }
    }
    if (roundManager->health_opponent != 0) {
        get_tree()->spawn(roundManager->EndTurn(playerCanGoAgain));
    }
}

// gd: L205-L215
void ShotgunShooting::PlayShootingSound() {
    const std::string currentRoundInChamber = shellSpawner->sequenceArray[0];
    if (currentRoundInChamber == "live") {
        CheckIfFinalShot();
        speaker_live->play();
        roundManager->playerData->stat_shotsFired += 1;
        animator_muzzleFlash->play("muzzle flash fire");
        animator_muzzleFlash_model->play("fire");
    } else {
        speaker_blank->play();
    }
}

// gd: L221-L230
void ShotgunShooting::CheckIfFinalShot() {
    temphealth_dealer = roundManager->health_opponent - roundManager->currentShotgunDamage;
    temphealth_player = roundManager->health_player - roundManager->currentShotgunDamage;
    if (!fired && roundManager->playerData->currentBatchIndex == 2 && !roundManager->endless) {
        if (temphealth_player <= 2 && whoshot == "player") {
            roundManager->musicManager->EndTrack();
            fired = true;
        }
        if (temphealth_dealer <= 2 && whoshot == "dealer") {
            roundManager->musicManager->EndTrack();
            fired = true;
        }
    }
}

// gd: L232-L238
void ShotgunShooting::ShotgunCollider(bool isEnabled) {
    if (isEnabled) {
        collider_shotgun->collision_layer = 1;
        collider_shotgun->collision_mask = 1;
    } else {
        collider_shotgun->collision_layer = 0;
        collider_shotgun->collision_mask = 0;
    }
}

// gd: L194-L197
void ShotgunShooting::CheckAchievement_coinflip() {
    const auto& turn = roundManager->playerCurrentTurnItemArray;
    const auto has = [&](const char* item) { return std::find(turn.begin(), turn.end(), item) != turn.end(); };
    bool setting = false;
    if (!has("magnifying glass") && !has("burner phone")) setting = true;
    const auto& seq = shellSpawner->sequenceArray;
    if (std::count(seq.begin(), seq.end(), "live") == std::count(seq.begin(), seq.end(), "blank") && setting) {
        ach->UnlockAchievement("ach8");
    }
}

// gd: L199-L200
void ShotgunShooting::CheckAchievement_why() {
    const auto& turn = roundManager->playerCurrentTurnItemArray;
    if (std::find(turn.begin(), turn.end(), "magnifying glass") != turn.end()) ach->UnlockAchievement("ach13");
}

// gd: L202-L203
void ShotgunShooting::CheckAchievement_style() {
    const auto& turn = roundManager->playerCurrentTurnItemArray;
    if (std::find(turn.begin(), turn.end(), "handsaw") != turn.end()) ach->UnlockAchievement("ach15");
}

}  // namespace scripts
