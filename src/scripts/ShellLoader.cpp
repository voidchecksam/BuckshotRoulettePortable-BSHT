#include "scripts/ShellLoader.h"

#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/CameraManager.h"
#include "scripts/CursorManager.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/DialogueManager.h"
#include "scripts/PermissionManager.h"
#include "scripts/RoundManager.h"
#include "scripts/resources.h"

namespace scripts {

// gd: L21-L26
seq::Task ShellLoader::DealerHandsGrabShotgun() {
    animator_dealerHands->play("dealer grab shotgun");
    co_await get_tree()->create_timer(.45);
    dealerAI->Speaker_HandCrack();
    shotgunHand_L->set_visible(true);
    shotgunHand_R->set_visible(true);
}

// gd: L28-L32
void ShellLoader::DealerHandsDropShotgun() {
    shotgunHand_L->set_visible(false);
    shotgunHand_R->set_visible(false);
    animator_dealerHands->play("RESET");
}

// gd: L34-L99
seq::Task ShellLoader::LoadShells() {
    camera->BeginLerp("enemy");
    if (!roundManager->shellLoadingSpedUp) co_await get_tree()->create_timer(.8);
    co_await DealerHandsGrabShotgun();
    co_await get_tree()->create_timer(.2);
    animator_shotgun->play("grab shotgun_pointing enemy");
    co_await get_tree()->create_timer(.45);
    if (roundManager->playerData->numberOfDialogueRead < 3) {
        if (diaindex == static_cast<int>(loadingDialogues.size())) {
            diaindex = 0;
        }
        std::string stringshow;
        if (diaindex == 0) stringshow = godot::tr("SHELL INSERT1");
        if (diaindex == 1) stringshow = godot::tr("SHELL INSERT2");
        get_tree()->spawn(dialogue->ShowText_ForDuration(stringshow, 3));
        diaindex += 1;
        co_await get_tree()->create_timer(3);
        roundManager->playerData->numberOfDialogueRead += 1;
    }
    const int numberOfShells =
        roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)]->amountBlank +
        roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)]->amountLive;
    for (int i = 0; i < numberOfShells; ++i) {
        speaker_loadShell->play();
        animator_dealerHandRight->play("load single shell");
        if (roundManager->shellLoadingSpedUp) {
            co_await get_tree()->create_timer(.17);
        } else {
            co_await get_tree()->create_timer(.32);
        }
    }
    animator_dealerHandRight->play("RESET");
    dealerAI->Speaker_HandCrack();
    if (roundManager->shellLoadingSpedUp) {
        co_await get_tree()->create_timer(.17);
    } else {
        co_await get_tree()->create_timer(.42);
    }
    //INTRODUCTION DIALOGUE
    if (roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)]->hasIntroductoryText) {
        dialogue->ShowText_Forever(introductionDialogues[0]);
        co_await get_tree()->create_timer(1.9);
        dialogue->ShowText_Forever(introductionDialogues[1]);
        co_await get_tree()->create_timer(3);
        dialogue->ShowText_Forever(introductionDialogues[2]);
        co_await get_tree()->create_timer(3);
        dialogue->ShowText_Forever(introductionDialogues[3]);
        co_await get_tree()->create_timer(3);
        dialogue->ShowText_Forever(introductionDialogues[4]);
        co_await get_tree()->create_timer(3);
        dialogue->ShowText_Forever(introductionDialogues[5]);
        co_await get_tree()->create_timer(3.7);
        dialogue->ShowText_Forever(introductionDialogues[6]);
        co_await get_tree()->create_timer(3.7);
        dialogue->ShowText_Forever(introductionDialogues[7]);
        co_await get_tree()->create_timer(3.7);
        dialogue->ShowText_Forever(introductionDialogues[8]);
        co_await get_tree()->create_timer(2.5);
        roundManager->playerData->hasReadIntroduction = true;
        dialogue->HideText();
    }
    //RACK SHOTGUN, PLACE ON TABLE
    animator_shotgun->play("enemy rack shotgun start");
    co_await get_tree()->create_timer(.8);
    animator_shotgun->play("enemy put down shotgun");
    DealerHandsDropShotgun();
    camera->BeginLerp("home");
    //ALLOW INTERACTION
    roundManager->playerCurrentTurnItemArray.clear();
    co_await get_tree()->create_timer(.6);
    perm->SetStackInvalidIndicators();
    cursor->SetCursor(true, true);
    perm->SetIndicators(true);
    perm->SetInteractionPermissions(true);
    roundManager->SetupDeskUI();
}

}  // namespace scripts
