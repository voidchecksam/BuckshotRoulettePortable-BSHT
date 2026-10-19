#include "scripts/RoundManager.h"

#include "scripts/AchievementManager.h"
#include "scripts/BriefcaseMachine.h"
#include "scripts/EndingManager.h"

#include "scripts/SignatureManager.h"
#include "scripts/StatueManager.h"
#include "scripts/SegmentManager.h"
#include "scripts/ItemAmounts.h"

#include "scripts/DealerIntelligence.h"
#include "scripts/DeathManager.h"
#include "scripts/DefibCutter.h"

#include "godot/array_ops.h"
#include "godot/math_funcs.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/CameraManager.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/DialogueManager.h"
#include "scripts/HandcuffManager.h"
#include "scripts/HealthCounter.h"
#include "scripts/InteractionBranch.h"
#include "scripts/ItemManager.h"
#include "scripts/MusicManager.h"
#include "scripts/PermissionManager.h"
#include "scripts/RoundBatch.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/ShellLoader.h"
#include "scripts/ShellSpawner.h"
#include "scripts/resources.h"

#include <algorithm>
#include <cstdint>

namespace scripts {

// gd: L62-L66
void RoundManager::_ready() {
    get_tree()->set_time_scale(1);
    HideDealer();
}

// gd: L68-L70
void RoundManager::_process(double /*delta*/) {
    LerpScore();
    InitialTimer();
}

// gd: L74-L75
void RoundManager::InitialTimer() {
    if (counting) initial_time += get_process_delta_time();
}

// gd: L77-L78
void RoundManager::BeginMainGame() {
    get_tree()->spawn(MainBatchSetup(true));
}

// gd: L80-L82
void RoundManager::HideDealer() {
    animator_dealerHands->play("hide hands");
    animator_dealer->play("hide dealer");
}

//MAIN BATCH SETUP
// gd: L87-L134
seq::Task RoundManager::MainBatchSetup(bool dealerEnterAtStart) {
    if (!enteringFromWaiver) {
        if (lerping) camera->BeginLerp("enemy");
        currentRound = 0;
        if (!dealerAtTable && dealerEnterAtStart) {
            co_await get_tree()->create_timer(.5);
            if (!dealerCuffed) {
                animator_dealerHands->play("dealer hands on table");
            } else {
                animator_dealerHands->play("dealer hands on table cuffed");
            }
            animator_dealer->play("dealer return to table");
            co_await get_tree()->create_timer(2);
            bool greeting = true;
            if (!playerData->hasSignedWaiver) {
                shellLoader->dialogue->ShowText_Forever(godot::tr("WAIVER"));
                co_await get_tree()->create_timer(2.3);
                shellLoader->dialogue->HideText();
                camera->BeginLerp("home");
                // gd: sign.AwaitPickup(), called without await: spawned, owned by the signature manager.
                get_tree()->spawn(sign->AwaitPickup(), sign->get_node_owner());
                co_return;
            }
            if (!dealerHasGreeted && greeting) {
                std::string tempstring;
                if (!playerData->enteringFromTrueDeath) {
                    tempstring = godot::tr("WELCOME");
                } else {
                    shellSpawner->dialogue->dealerLowPitched = true;
                    tempstring = "...";
                }
                if (!playerData->playerEnteringFromDeath) {
                    shellLoader->dialogue->ShowText_Forever("...");
                    co_await get_tree()->create_timer(2.3);
                    shellLoader->dialogue->HideText();
                    dealerHasGreeted = true;
                } else {
                    shellLoader->dialogue->ShowText_Forever(tempstring);
                    co_await get_tree()->create_timer(2.3);
                    shellLoader->dialogue->HideText();
                    dealerHasGreeted = true;
                }
            }
            dealerAtTable = true;
        }
    }
    enteringFromWaiver = false;
    playerData->enteringFromTrueDeath = false;
    mainBatchIndex = playerData->currentBatchIndex;
    healthCounter->DisableCounter();
    SetupRoundArray();
    if (playerData->hasReadIntroduction) {
        roundArray[0]->hasIntroductoryText = false;
    } else {
        roundArray[0]->hasIntroductoryText = true;
    }
    if (roundArray[0]->showingIndicator) co_await RoundIndicator();
    healthCounter->SetupHealth();
    lerping = true;
    if (!endless) ParseMainGameAmounts();
    get_tree()->spawn(StartRound(false));
}

// gd: L137-L139
void RoundManager::ParseMainGameAmounts() {
    for (AmountResource* res : amounts->array_amounts) res->amount_active = res->amount_main;
}

// gd: L142-L157. The RoundClass resources are the scene's own (shared, as
// GDScript shares them): an endless batch rewrites them in place.
void RoundManager::GenerateRandomBatches() {
    for (godot::Node* node : batchArray) {
        auto* b = godot::script_cast<RoundBatch>(node);
        if (!b) continue;
        for (std::size_t i = 0; i < b->roundArray.size(); ++i) {
            b->roundArray[i]->startingHealth = static_cast<int>(godot::randi_range(2, 4));
            curhealth = b->roundArray[i]->startingHealth;

            const int total_shells = static_cast<int>(godot::randi_range(2, 8));
            const int amount_live = std::max(1, total_shells / 2);
            const int amount_blank = total_shells - amount_live;
            b->roundArray[i]->amountBlank = amount_blank;
            b->roundArray[i]->amountLive = amount_live;

            b->roundArray[i]->numberOfItemsToGrab = static_cast<int>(godot::randi_range(2, 5));
            b->roundArray[i]->usingItems = true;
            const int64_t flip = godot::randi_range(0, 1);
            if (flip == 1) b->roundArray[i]->shufflingArray = true;
        }
    }
}

//APPEND ROUNDS FROM BATCH
// gd: L160-L169
void RoundManager::SetupRoundArray() {
    if (endless) GenerateRandomBatches();
    roundArray.clear();
    for (std::size_t i = 0; i < batchArray.size(); ++i) {
        const auto* batch = godot::script_cast<RoundBatch>(batchArray[i]);
        if (batch && batch->batchIndex == mainBatchIndex) {
            const RoundBatch* matched = batch;
            for (std::size_t z = 0; z < matched->roundArray.size(); ++z) {
                roundArray.push_back(matched->roundArray[z]);
            }
        }
    }
}

//SHOW ROUND INDICATOR
// gd: L173-L193
seq::Task RoundManager::RoundIndicator() {
    roundIndicator->set_visible(false);
    animator_roundIndicator->play("RESET");
    camera->BeginLerp("health counter");
    co_await get_tree()->create_timer(.8);
    statue->CheckStatus();
    const godot::Vector3 activePos =
        roundIndicatorPositions[static_cast<std::size_t>(roundArray[0]->indicatorNumber)];
    roundIndicator->set_transform_origin(activePos);
    roundIndicatorParent->set_visible(true);
    speaker_roundHum->play();
    co_await get_tree()->create_timer(.8);
    roundIndicator->set_visible(true);
    brief->ending->endless_roundsbeat += 1;
    animator_roundIndicator->play("round blinking");
    co_await get_tree()->create_timer(2);
    roundIndicatorParent->set_visible(false);
    speaker_roundHum->stop();
    speaker_roundShutDown->play();
    animator_roundIndicator->play("RESET");
}

//MAIN ROUND SETUP
// gd: L196-L234
seq::Task RoundManager::StartRound(bool gettingNext) {
    if (gettingNext && (currentRound + 1) != static_cast<int>(roundArray.size())) currentRound += 1;
    // gd: #UNCUFF BOTH PARTIES BEFORE ITEM DISTRIBUTION
    co_await handcuffs->RemoveAllCuffsRoutine();
    //FINAL SHOWDOWN DIALOGUE
    if (playerData->currentBatchIndex == 2 && !defibCutterReady && !endless) {
        shellLoader->dialogue->dealerLowPitched = true;
        camera->BeginLerp("enemy");
        co_await get_tree()->create_timer(.6);
        if (!playerData->cutterDialogueRead) {
            shellLoader->dialogue->scaling = true;
            shellLoader->dialogue->ShowText_Forever(godot::tr("FINAL SHOW1"));
            co_await get_tree()->create_timer(4);
            shellLoader->dialogue->scaling = true;
            shellLoader->dialogue->ShowText_Forever(godot::tr("FINAL SHOW2"));
            co_await get_tree()->create_timer(4);
            shellLoader->dialogue->scaling = true;
            shellLoader->dialogue->ShowText_Forever(godot::tr("FINAL SHOW3"));
            co_await get_tree()->create_timer(4.8);
            shellLoader->dialogue->scaling = false;
            shellLoader->dialogue->HideText();
            playerData->cutterDialogueRead = true;
        } else {
            shellLoader->dialogue->ShowText_Forever(godot::tr("BETTER NOT"));
            co_await get_tree()->create_timer(3);
            shellLoader->dialogue->HideText();
        }
        co_await deficutter->InitialSetup();
        defibCutterReady = true;
        trueDeathActive = true;
    }
    //USINGITEMS: SHARE ITEMS TO PLAYERS HERE
    if (roundArray[static_cast<std::size_t>(currentRound)]->usingItems) {
        // gd: called without await, so it runs to its first await and returns;
        // spawning it is the same thing here.
        get_tree()->spawn(itemManager->BeginItemGrabbing());
        co_return;
    }
    get_tree()->spawn(shellSpawner->MainShellRoutine());
}

// gd: L237-L239
void RoundManager::ReturnFromItemGrabbing() {
    get_tree()->spawn(shellSpawner->MainShellRoutine());
}

// gd: L241-L243
void RoundManager::LoadShells() {
    get_tree()->spawn(shellLoader->LoadShells());
}

// gd: L297-L306
seq::Task RoundManager::ReturnFromCuffCheck(bool brokeFree) {
    if (brokeFree) {
        co_await get_tree()->create_timer(.8);
        camera->BeginLerp("enemy");
        dealerAI->BeginDealerTurn();
    } else {
        camera->BeginLerp("home");
        // gd: BeginPlayerTurn(), not awaited: spawned.
        get_tree()->spawn(BeginPlayerTurn());
    }
}

// gd: L357-L362
seq::Task RoundManager::OutOfHealth(std::string who) {
    if (who == "player") {
        // gd: death.MainDeathRoutine(), not awaited: spawned.
        if (auto* dm = godot::script_cast<DeathManager>(death)) get_tree()->spawn(dm->MainDeathRoutine());
    } else {
        co_await get_tree()->create_timer(1);
        // gd: EndMainBatch(), not awaited: spawned.
        get_tree()->spawn(EndMainBatch());
    }
    co_return;
}

// gd: L466-L480
seq::Task RoundManager::RestartBatch() {
    playerData->currentBatchIndex = 0;
    if (barrelSawedOff) {
        co_await get_tree()->create_timer(.6);
        co_await segmentManager->GrowBarrel();
    }
    // gd: MainBatchSetup(false), not awaited: spawned, owned by this manager.
    get_tree()->spawn(MainBatchSetup(false), get_node_owner());
    if (!dealerAtTable) {
        if (!dealerCuffed) animator_dealerHands->play("dealer hands on table");
        else animator_dealerHands->play("dealer hands on table cuffed");
        animator_dealer->play("dealer return to table");
    }
    for (godot::Node* n : ejectManagers) {
        if (auto* eject = godot::script_cast<ShellEjectManager>(n)) eject->FadeOutShell();
    }
    // TRACK MANAGER
    co_await get_tree()->create_timer(2);
    musicManager->LoadTrack_FadeIn();
}

// gd: L482-L528
seq::Task RoundManager::EndMainBatch() {
    // ADD TO BATCH INDEX
    ignoring = true;
    playerData->currentBatchIndex += 1;
    // PLAY WINNING SHIT
    co_await get_tree()->create_timer(.8);
    if (playerData->currentBatchIndex == 3) {
        healthCounter->speaker_truedeath->stop();
        healthCounter->DisableCounter();
        // gd: defibCutter.BlipError_Both(), not awaited: spawned.
        get_tree()->spawn(defibCutter->BlipError_Both());
        if (endless) musicManager->EndTrack();
        co_await get_tree()->create_timer(.4);
        if (endless) {
            counting = false;
            // gd: BeginScoreLerp(), not awaited: spawned, owned by this manager.
            get_tree()->spawn(BeginScoreLerp(), get_node_owner());
            co_return;
        }
        // gd: #gameover.PlayerWon()
        camera->BeginLerp("enemy");
        co_await get_tree()->create_timer(.7);
        // gd: brief.MainRoutine(), not awaited: spawned, owned by the machine.
        get_tree()->spawn(brief->MainRoutine(), brief->get_node_owner());
        co_return;
    }
    healthCounter->DisableCounter();
    speaker_roundShutDown->play();
    co_await get_tree()->create_timer(1);
    speaker_winner->play();
    ui_winner->set_visible(true);
    itemManager->newBatchHasBegun = true;
    co_await get_tree()->create_timer(2.33);
    speaker_roundShutDown->play();
    speaker_winner->stop();
    musicManager->EndTrack();
    ui_winner->set_visible(false);
    // REGROW BARREL IF MISSING
    if (barrelSawedOff) {
        co_await get_tree()->create_timer(.6);
        co_await segmentManager->GrowBarrel();
    }
    // MAIN BATCH LOOP
    get_tree()->spawn(MainBatchSetup(false));
    if (!dealerAtTable) {
        if (!dealerCuffed) animator_dealerHands->play("dealer hands on table");
        else animator_dealerHands->play("dealer hands on table cuffed");
        animator_dealer->play("dealer return to table");
    }
    for (godot::Node* n : ejectManagers) {
        if (auto* eject = godot::script_cast<ShellEjectManager>(n)) eject->FadeOutShell();
    }
    // TRACK MANAGER
    co_await get_tree()->create_timer(2);
    musicManager->LoadTrack_FadeIn();
}

// gd: L264-L295
seq::Task RoundManager::EndTurn(bool playerCanGoAgain) {
    // USINGITEMS: ASSIGN PLAYER CAN GO AGAIN FROM ITEMS HERE
    // USINGITEMS: MAKE SHOTGUN GROW NEW BARREL
    // gd: the CheckIfOutOfHealth guard above is commented out in the original.
    if (barrelSawedOff) {
        co_await get_tree()->create_timer(.6);
        if (waitingForHealthCheck2) co_await get_tree()->create_timer(2);
        waitingForHealthCheck2 = false;
        co_await segmentManager->GrowBarrel();
    }
    if (!shellSpawner->sequenceArray.empty()) {
        // PLAYER TURN
        if (playerCanGoAgain) {
            get_tree()->spawn(BeginPlayerTurn());
        } else {
            // DEALER TURN
            if (!dealerCuffed) {
                dealerAI->BeginDealerTurn();
            } else {
                if (waitingForReturn) {
                    co_await get_tree()->create_timer(1.4);
                    waitingForReturn = false;
                }
                if (waitingForHealthCheck) {
                    co_await get_tree()->create_timer(1.8);
                    waitingForHealthCheck = false;
                }
                // gd: dealerAI.DealerCheckHandCuffs(), not awaited: spawned.
                get_tree()->spawn(dealerAI->DealerCheckHandCuffs());
            }
        }
    } else {
        // gd: #SHOTGUN IS EMPTY. NEXT ROUND
        if (requestedWireCut) co_await defibCutter->CutWire(wireToCut);
        if (!ignoring) get_tree()->spawn(StartRound(true));
    }
}

// gd: L308-L334
seq::Task RoundManager::BeginPlayerTurn() {
    if (playerCuffed) {
        bool returning = false;
        if (playerAboutToBreakFree == false) {
            // gd: handcuffs.CheckPlayerHandCuffs(false), not awaited: spawned.
            get_tree()->spawn(handcuffs->CheckPlayerHandCuffs(false));
            co_await get_tree()->create_timer(1.4);
            camera->BeginLerp("enemy");
            dealerAI->BeginDealerTurn();
            returning = true;
            playerAboutToBreakFree = true;
        } else {
            // gd: handcuffs.BreakPlayerHandCuffs(false), not awaited: spawned.
            get_tree()->spawn(handcuffs->BreakPlayerHandCuffs(false));
            co_await get_tree()->create_timer(1.4);
            camera->BeginLerp("home");
            playerCuffed = false;
            playerAboutToBreakFree = false;
            returning = false;
        }
        if (returning) co_return;
    }
    if (requestedWireCut) co_await defibCutter->CutWire(wireToCut);
    co_await get_tree()->create_timer(.6);
    playerCurrentTurnItemArray.clear();
    perm->SetStackInvalidIndicators();
    cursor->SetCursor(true, true);
    perm->SetIndicators(true);
    perm->SetInteractionPermissions(true);
    SetupDeskUI();
}

// gd: L340-L348
void RoundManager::SetupDeskUI() {
    deskUI_parent->set_visible(true);
    deskUI_shotgun->set_visible(true);
    if (roundArray[static_cast<std::size_t>(currentRound)]->usingItems) {
        for (godot::Control* b : deskUI_grids) b->set_visible(true);
    } else {
        for (godot::Control* b : deskUI_grids) b->set_visible(false);
    }

    if (cursor->controller_active) deskUI_shotgun->grab_focus();
    controller->previousFocus = deskUI_shotgun;
    ++port_playerTurnsBegun;
}

// gd: L350-L355
void RoundManager::ClearDeskUI(bool includingParent) {
    if (includingParent) deskUI_parent->set_visible(false);
    deskUI_shotgun->set_visible(false);
    for (godot::Control* b : deskUI_grids) b->set_visible(false);
    controller->previousFocus = nullptr;
}

// gd: L385-L426
seq::Task RoundManager::BeginScoreLerp() {
    startscore = prevscore;
    if (!doubling) {
        double_or_nothing_rounds_beat += 1;
        const int ten_minutes_seconds = 600;
        const int ten_minutes_score_loss = 40000;
        const double score_deduction = initial_time / ten_minutes_seconds * ten_minutes_score_loss;
        endscore = 70000 - static_cast<int>(score_deduction);
        if (endscore < 10) endscore = 10;
        prevscore = endscore;
        double_or_nothing_score = prevscore;
        double_or_nothing_initial_score = prevscore;
    } else {
        doubled = true;
        endscore = prevscore * 2;
        prevscore = endscore;
        double_or_nothing_rounds_beat += 1;
        double_or_nothing_score = prevscore;
    }
    doubling = true;
    speaker_slot->play();
    camera->BeginLerp("yes no");
    co_await get_tree()->create_timer(1.1);
    ui_score->set_visible(true);
    ui_score->set_text(godot::str(startscore));
    co_await get_tree()->create_timer(.5);
    elapsed = 0;
    lerpingscore = true;
    co_await get_tree()->create_timer(3.08);
    co_await get_tree()->create_timer(.46);
    ui_score->set_visible(false);
    ui_doubleornothing->set_visible(true);
    anim_doubleor->play("show");
    speaker_show->play();
    co_await get_tree()->create_timer(.5);
    co_await get_tree()->create_timer(1);
    cursor->SetCursor(true, true);
    intbranch_no->interactionAllowed = true;
    intbranch_yes->interactionAllowed = true;
    btnParent_doubleor->set_visible(true);
    if (cursor->controller_active) btn_yes->grab_focus();
    controller->previousFocus = btn_yes;
}

// gd: L428-L429
void RoundManager::RevertDoubleUI() {
    btnParent_doubleor->set_visible(false);
}

// gd: L432-L457
seq::Task RoundManager::Response(bool rep) {
    RevertDoubleUI();
    intbranch_no->interactionAllowed = false;
    intbranch_yes->interactionAllowed = false;
    cursor->SetCursor(false, false);
    ui_doubleornothing->set_visible(false);
    if (rep) anim_yes->play("press");
    else anim_no->play("press");
    speaker_key->play();
    co_await get_tree()->create_timer(.4);
    anim_doubleor->play("hide");
    speaker_hide->play();
    co_await get_tree()->create_timer(.4);
    if (!rep) {
        speaker_slot->stop();
        co_await get_tree()->create_timer(.7);
        brief->ending->endless_score = endscore;
        brief->ending->endless_overwriting = true;
        camera->BeginLerp("enemy");
        // gd: brief.MainRoutine(), not awaited: spawned, owned by the machine.
        get_tree()->spawn(brief->MainRoutine(), brief->get_node_owner());
    } else {
        speaker_slot->stop();
        co_await get_tree()->create_timer(.7);
        // gd: #camera.BeginLerp("enemy")
        // gd: RestartBatch(), not awaited: spawned, owned by this manager.
        get_tree()->spawn(RestartBatch(), get_node_owner());
    }
}

// gd: L459-L464
void RoundManager::LerpScore() {
    if (lerpingscore) {
        elapsed += get_process_delta_time();
        const double c = godot::clampf(elapsed / dur, 0.0, 1.0);
        const double score = godot::lerp(static_cast<double>(startscore), static_cast<double>(endscore), c);
        ui_score->set_text(godot::str(static_cast<int>(score)));
    }
}

}  // namespace scripts
