#include "scripts/DeathManager.h"

#include "godot/print.h"
#include "scripts/AchievementManager.h"
#include "scripts/SaveFileManager.h"
#include "scripts/resources.h"
#include "scripts/CameraManager.h"

#include "godot/scene_tree.h"
#include "scripts/CameraShaker.h"
#include "scripts/FilterController.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/HealthCounter.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/ShellLoader.h"
#include "scripts/ShellSpawner.h"
#include "scripts/ShotgunShooting.h"
#include "scripts/SpeakerController.h"

namespace scripts {

// gd: L27-L28
void DeathManager::_ready() {
    defibParent->set_visible(false);
}

// gd: L139-L160
seq::Task DeathManager::MainDeathRoutine() {
    bool loadingHeaven = false;
    if (shotgunShooting->roundManager->endless) {
        // gd: `if (rm.endscore != null)`; endscore is an int here, never null.
        if (rm->endscore > 1000000) ach->UnlockAchievement("ach10");
        co_await get_tree()->create_timer(.5);
        godot::print("changing scene to: death");
        get_tree()->change_scene_to_file("res://scenes/death.tscn");
        co_return;
    }
    if (shotgunShooting->roundManager->wireIsCut_player) {
        shotgunShooting->roundManager->playerData->enteringFromTrueDeath = true;
        loadingHeaven = true;
    }
    shotgunShooting->roundManager->playerData->playerEnteringFromDeath = true;
    // gd: await savefile.SaveGame(). SaveGame is not a coroutine, so the await
    // returns at once.
    if (savefile) savefile->SaveGame();
    co_await get_tree()->create_timer(.5);
    if (!loadingHeaven) {
        godot::print("changing scene to: death");
        get_tree()->change_scene_to_file("res://scenes/death.tscn");
    } else {
        godot::print("changing scene to: heaven");
        get_tree()->change_scene_to_file("res://scenes/heaven.tscn");
    }
}

// gd: L31-L112
seq::Task DeathManager::Kill(std::string who, bool trueDeath, bool returningShotgun) {
    bool dealerKilledSelf = false;
    shitIsFuckedUp = false;
    if (who == "player") {
        if (trueDeath) {
            // gd: `pass`; MainDeathRoutine runs from OutOfHealth instead.
        } else {
            co_await get_tree()->create_timer(.08);
            viewblocker->set_visible(true);
            if (returningShotgun) {
                bool addingDelay = false;
                animator_shotgun->play("RESET");
                ejectManager_player->DeathEjection();
                // gd: `await get_tree().create_timer(2)` awaits the timer
                // itself, not its timeout signal, and GDScript returns from
                // awaiting a plain object at once: the original waits nothing
                // here, so neither does the port.
                if (shotgunShooting->roundManager->health_opponent == 1 ||
                    shotgunShooting->roundManager->health_player == 1) {
                    addingDelay = true;
                }
                // gd: the `delaying` line here is commented out in the original.
                if (shotgunShooting->roundManager->health_player == 1) shitIsFuckedUp = true;
                if (shotgunShooting->roundManager->health_player != 0) {
                    get_tree()->spawn(shotgunShooting->FinalizeShooting(
                        shotgunShooting->playerCanGoAgain, false, true, addingDelay));
                }
            }
            DisableSpeakers();
            if (shotgunShooting->roundManager->health_player == 0) {
                get_tree()->spawn(shotgunShooting->roundManager->OutOfHealth("player"));
                co_return;
            }
            co_await get_tree()->create_timer(.4);
            speaker_playerDefib->play();
            co_await get_tree()->create_timer(.85);
            speaker_heartbeat->play();
            animator_pp->play("revival brightness");
            defibParent->set_visible(true);
            animator_playerDefib->play("RESET");
            viewblocker->set_visible(false);
            filter->BeginPan(filter->lowPassMaxValue, filter->lowPassDefaultValue);
            FadeInSpeakers();
            cameraShaker->Shake();
            co_await get_tree()->create_timer(.6);
            animator_playerDefib->play("remove defib device");
            co_await get_tree()->create_timer(.4);
            co_await healthCounter->UpdateDisplayRoutine(false, !shotgunShooting->playerCanGoAgain, false);
            defibParent->set_visible(false);
        }
    } else if (who == "dealer") {
        if (trueDeath) {
            // gd: `pass`
        } else {
            if (returningShotgun) {
                dealerKilledSelf = true;
                shotgunShooting->MainSlowDownRoutine("dealer", true);
                animator_shotgun->play("enemy return shotgun self");
                dealerAI->dealerHoldingShotgun = false;
                ejectManager_dealer->DeathEjection();
            }
            animator_dealer->play("dealer fly away");
            animator_dealerHands->play("hide hands");
            dealerAI->SwapDealerMesh();
            shellLoader->shotgunHand_L->set_visible(false);
            shellLoader->shotgunHand_R->set_visible(false);
            shotgunShooting->roundManager->dealerAtTable = false;
            co_await get_tree()->create_timer(.4);
            speaker_crash->play();
            if (!dealerKilledSelf) {
                // player shot dealer. eject shell and end turn here.
                get_tree()->spawn(shotgunShooting->ShootingDealerEjection(
                    shotgunShooting->shellSpawner->sequenceArray[0], "dealer", false));
            }
            co_await get_tree()->create_timer(1.8);
            if (shotgunShooting->roundManager->health_opponent == 0) {
                get_tree()->spawn(shotgunShooting->roundManager->OutOfHealth("dealer"));
                get_tree()->spawn(healthCounter->UpdateDisplayRoutine(false, false, true));
                co_return;
            }
            if (shotgunShooting->roundManager->health_player == 0) {
                get_tree()->spawn(shotgunShooting->roundManager->OutOfHealth("player"));
                co_return;
            }
            healthCounter->checkingPlayer = dealerKilledSelf;
            co_await healthCounter->UpdateDisplayRoutine(false, true, false);
            if (!shotgunShooting->roundManager->dealerCuffed) {
                animator_dealerHands->play("dealer hands on table");
                shotgunShooting->roundManager->waitingForDealerReturn = true;
            } else {
                animator_dealerHands->play("dealer hands on table cuffed");
                shotgunShooting->roundManager->waitingForReturn = true;
            }
            animator_dealer->play("dealer return to table");
            co_await get_tree()->create_timer(2);
            if (dealerKilledSelf) dealerAI->EndDealerTurn(dealerAI->dealerCanGoAgain);
        }
    }
}

// gd: L162-L164
void DeathManager::DisableSpeakers() {
    for (std::size_t i = 0; i < speakersToDisable.size(); i++) {
        speakersToDisable[i]->SnapVolume(false);
    }
}

// gd: L166-L169
void DeathManager::FadeInSpeakers() {
    for (std::size_t i = 0; i < speakersToDisable.size(); i++) {
        speakersToDisable[i]->fadeDuration = 3;
        speakersToDisable[i]->FadeIn();
    }
}

// gd: L114-L136
seq::Task DeathManager::MedicineDeath() {
    viewblocker->set_visible(true);
    const godot::Vector3 r = cam->cam->get_rotation_degrees();
    cam->cam->set_rotation_degrees({r.x, r.y, 0.0f});
    DisableSpeakers();
    if (shotgunShooting->roundManager->health_player == 0) {
        // gd: OutOfHealth("player"), not awaited: spawned.
        get_tree()->spawn(shotgunShooting->roundManager->OutOfHealth("player"));
        co_return;
    }
    co_await get_tree()->create_timer(.4);
    speaker_playerDefib->play();
    co_await get_tree()->create_timer(.85);
    speaker_heartbeat->play();
    animator_pp->play("revival brightness");
    defibParent->set_visible(true);
    animator_playerDefib->play("RESET");
    viewblocker->set_visible(false);
    filter->BeginPan(filter->lowPassMaxValue, filter->lowPassDefaultValue);
    FadeInSpeakers();
    cameraShaker->Shake();
    co_await get_tree()->create_timer(.6);
    animator_playerDefib->play("remove defib device");
    co_await get_tree()->create_timer(.4);
    // gd: #await(healthCounter.UpdateDisplayRoutine(...)), commented out in the .gd.
    defibParent->set_visible(false);
}

}  // namespace scripts
