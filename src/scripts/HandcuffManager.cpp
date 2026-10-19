#include "scripts/HandcuffManager.h"
#include "scripts/RoundManager.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/CameraManager.h"
#include "godot/scene_tree.h"

namespace scripts {

bool HandcuffManager::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "AttachHandCuffs") {
        get_tree()->spawn(AttachHandCuffs());
        return true;
    }
    if (method == "CheckPlayerHandCuffs_Animation") {
        get_tree()->spawn(CheckPlayerHandCuffs_Animation());
        return true;
    }
    return false;
}

// gd: L11-L14
seq::Task HandcuffManager::AttachHandCuffs() {
    animator_handcuffsPlayer->play("RESET");
    co_await get_tree()->create_timer(.1);
    handcuffs_player->set_visible(true);
}

// gd: L16-L22
seq::Task HandcuffManager::CheckPlayerHandCuffs(bool lerpingToPrevious) {
    previousSocket = camera->activeSocket;
    camera->BeginLerp("check handcuffs");
    co_await get_tree()->create_timer(.6);
    animator_handcuffsPlayer->play("check handcuffs");
    co_await get_tree()->create_timer(.8);
    if (lerpingToPrevious) camera->BeginLerp(previousSocket);
}

// gd: L24-L30
seq::Task HandcuffManager::BreakPlayerHandCuffs(bool lerpingToPrevious) {
    previousSocket = camera->activeSocket;
    camera->BeginLerp("check handcuffs");
    co_await get_tree()->create_timer(.6);
    animator_handcuffsPlayer->play("player break handcuffs");
    co_await get_tree()->create_timer(.8);
    if (lerpingToPrevious) camera->BeginLerp(previousSocket);
}

// gd: L32-L38
seq::Task HandcuffManager::CheckPlayerHandCuffs_Animation() {
    previousSocket = camera->activeSocket;
    camera->BeginLerp("check handcuffs");
    co_await get_tree()->create_timer(.6);
    animator_handcuffsPlayer->play("check handcuffs");
    co_await get_tree()->create_timer(.8);
    camera->BeginLerp(previousSocket);
}

// gd: L40-L56
seq::Task HandcuffManager::RemoveAllCuffsRoutine() {
    if (roundManager->dealerCuffed) {
        camera->BeginLerp("enemy cuffs close");
        co_await get_tree()->create_timer(.3);
        dealerAI->animator_dealerHands->play("dealer break cuffs");
        dealerAI->speaker_breakHandcuffs->play();
        dealerAI->dealerAboutToBreakFree = false;
        roundManager->dealerCuffed = false;
        co_await get_tree()->create_timer(.47);
    }
    if (roundManager->playerCuffed) {
        camera->BeginLerp("check handcuffs");
        co_await get_tree()->create_timer(.6);
        animator_handcuffsPlayer->play("player break handcuffs");
        roundManager->playerCuffed = false;
        roundManager->playerAboutToBreakFree = false;
        co_await get_tree()->create_timer(.8);
    }
}

}  // namespace scripts
