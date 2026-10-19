#include "scripts/HealthCounter.h"
#include "scripts/DefibCutter.h"

#include "godot/array_ops.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/CameraManager.h"
#include "scripts/DialogueManager.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/resources.h"

namespace scripts {

bool HealthCounter::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "UpdateDisplayRoutineCigarette_Player") {
        UpdateDisplayRoutineCigarette_Player();
        return true;
    }
    if (method == "UpdateDisplayRoutineCigarette_Enemy") {
        UpdateDisplayRoutineCigarette_Enemy();
        return true;
    }
    return false;
}

// gd: L26-L28
void HealthCounter::_ready() {
    ClearDisplay();
}

// gd: L31-L42
void HealthCounter::SetupHealth() {
    if (roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)]->isFirstRound) {
        if (setting) {
            ui_playername->set_text(godot::to_upper(roundManager->playerData->playername));
            const std::string playername_upper = godot::to_upper(roundManager->playerData->playername);
            ui_playerwin->set_text(godot::format_s(godot::tr("PLAYERWIN"), playername_upper));
            setting = false;
        }
        roundManager->health_player =
            roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)]->startingHealth;
        roundManager->health_opponent =
            roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)]->startingHealth;
        dialogueEntered_player = false;
        dialogueEntered_dealer = false;
    }
}

// gd: L44-L54
void HealthCounter::UpdateDisplay() {
    for (std::size_t i = 0; i < symbolArray_player.size(); ++i) {
        if (static_cast<int>(i) <= roundManager->health_player) {
            symbolArray_player[i]->set_visible(true);
        } else {
            symbolArray_player[i]->set_visible(false);
        }
    }
    for (std::size_t i = 0; i < symbolArray_enemy.size(); ++i) {
        if (static_cast<int>(i) <= roundManager->health_opponent) {
            symbolArray_enemy[i]->set_visible(true);
        } else {
            symbolArray_enemy[i]->set_visible(false);
        }
    }
    FlickerLastDigit();
}

// gd: L56-L60
void HealthCounter::FlickerLastDigit() {
    if (roundManager->health_player == 1) {
        animator_flickerPlayer->play("flicker digit");
    } else {
        animator_flickerPlayer->play("RESET");
    }
    if (roundManager->health_opponent == 1) {
        animator_flickerEnemy->play("flicker digit");
    } else {
        animator_flickerEnemy->play("RESET");
    }
}

// gd: L66-L121
seq::Task HealthCounter::UpdateDisplayRoutine(bool isRaisingHealth, bool goingToPreviousSocket,
                                              bool showPlayerWin) {
    bool indicating = true;
    if (roundManager->dealerCuffed && playerShotSelf) {
        roundManager->waitingForHealthCheck = true;
        playerShotSelf = false;
    }
    if (showPlayerWin) {
        //ERROR - FLICKER BOTH HERE
        camera->BeginLerp("health counter");
        co_await get_tree()->create_timer(.8);
        UpdateDisplay();
        speaker_noise->play();
        co_return;
    }
    const std::string previousSocket = camera->activeSocket;
    camera->BeginLerp("health counter");
    co_await get_tree()->create_timer(.8);
    UpdateDisplay();
    if (checkingPlayer && defibCutter->displayBroken_player) {
        indicating = false;
        // gd: defibCutter.BlipError("player"), not awaited: spawned.
        get_tree()->spawn(defibCutter->BlipError("player"));
    }
    if (!checkingPlayer && defibCutter->displayBroken_dealer) {
        indicating = false;
        get_tree()->spawn(defibCutter->BlipError("dealer"));
    }
    if (indicating) {
        if (isRaisingHealth) {
            speaker_beep->play();
        } else {
            speaker_noise->play();
        }
    }
    if ((roundManager->health_opponent <= 2 || roundManager->health_player <= 2) &&
        roundManager->playerData->currentBatchIndex == 2 && !roundManager->endless) {
        if (roundManager->defibCutterReady && !trueDeathSetup) {
            speaker_truedeath->play();
            trueDeathSetup = true;
        }
        if (roundManager->health_opponent <= 2 && !roundManager->wireIsCut_dealer) {
            roundManager->wireToCut = "dealer";
            roundManager->requestedWireCut = true;
            roundManager->wireIsCut_dealer = true;
            roundManager->health_opponent = 1;
        }
        if (roundManager->health_player <= 2 && !roundManager->wireIsCut_player) {
            roundManager->wireToCut = "player";
            roundManager->requestedWireCut = true;
            roundManager->wireIsCut_player = true;
            roundManager->health_player = 1;
        }
    }
    co_await get_tree()->create_timer(.7);
    if (roundManager->health_player == 1 && !dialogueEntered_player && !roundManager->wireIsCut_player &&
        !skipping_careful) {
        if (!roundManager->wireIsCut_player) {
            get_tree()->spawn(dialogue->ShowText_ForDuration(godot::tr("CAREFUL"), 3));
        } else {
            get_tree()->spawn(dialogue->ShowText_ForDuration("...", 3));
        }
        co_await get_tree()->create_timer(3);
        dialogueEntered_player = true;
    }
    if (skipping_careful) skipping_careful = false;
    const bool lerpingCamera = true;
    if (roundManager->shellSpawner->sequenceArray.size() == 0) {
        //lerpingCamera = false
    }
    if (lerpingCamera) {
        if (goingToPreviousSocket) {
            camera->BeginLerp(previousSocket);
        } else {
            camera->BeginLerp("home");
        }
    }
    co_await get_tree()->create_timer(.4);
}

// gd: L126-L139
void HealthCounter::UpdateDisplayRoutineCigarette_Enemy() {
    const int maxHealth = roundManager->roundArray[0]->startingHealth;
    bool changingHealth = false;
    const int prevhealth = roundManager->health_opponent;
    if (!roundManager->wireIsCut_dealer && !overriding_medicine) roundManager->health_opponent += 1;
    if (overriding_medicine && !roundManager->wireIsCut_dealer) {
        if (overriding_medicine_adding) {
            roundManager->health_opponent += 2;
        } else {
            roundManager->health_opponent -= 1;
        }
    }
    if (roundManager->health_opponent > maxHealth) roundManager->health_opponent = maxHealth;
    const int newhealth = roundManager->health_opponent;
    if (newhealth != prevhealth) changingHealth = true;
    get_tree()->spawn(UpdateDisplayRoutineCigarette_Main(changingHealth, true));
}

// gd: L141-L153
void HealthCounter::UpdateDisplayRoutineCigarette_Player() {
    const int maxHealth = roundManager->roundArray[0]->startingHealth;
    bool changingHealth = false;
    const int prevhealth = roundManager->health_player;
    if (!roundManager->wireIsCut_player && !overriding_medicine) roundManager->health_player += 1;
    if (overriding_medicine && !roundManager->wireIsCut_player) {
        if (overriding_medicine_adding) roundManager->health_player += 2;
    }
    if (roundManager->health_player > maxHealth) roundManager->health_player = maxHealth;
    const int newhealth = roundManager->health_player;
    if (newhealth != prevhealth) changingHealth = true;
    if (overriding_medicine && !overriding_medicine_adding) changingHealth = true;
    get_tree()->spawn(UpdateDisplayRoutineCigarette_Main(changingHealth, false));
}

// gd: L155-L179
seq::Task HealthCounter::UpdateDisplayRoutineCigarette_Main(bool isChanging, bool isAddingEnemy) {
    const std::string prevsocket = camera->activeSocket;
    bool playingsound = true;
    camera->BeginLerp("health counter");
    co_await get_tree()->create_timer(.8);
    if (isAddingEnemy && defibCutter->displayBroken_dealer) {
        // gd: defibCutter.BlipError("dealer"), not awaited: spawned.
        get_tree()->spawn(defibCutter->BlipError("dealer"));
        playingsound = false;
    }
    if (!isAddingEnemy && defibCutter->displayBroken_player) {
        get_tree()->spawn(defibCutter->BlipError("player"));
        playingsound = false;
    }
    UpdateDisplay();
    if (!overriding_medicine) {
        if (isChanging && playingsound) speaker_beep->play();
    } else {
        if (isChanging && overriding_medicine_adding) {
            speaker_beep->play();
        } else if (isChanging && !overriding_medicine_adding) {
            speaker_noise->play();
        }
    }
    co_await get_tree()->create_timer(.7);
    if (!isAddingEnemy) {
        camera->BeginLerp("home");
        animator_playerHands->play("player revert cigarette");
    } else {
        camera->BeginLerp("enemy");
        animator_dealerHands->play("RESET");
    }
    overriding_medicine = false;
    (void)prevsocket;
}

// gd: L181-L184
void HealthCounter::SwapSkullSymbols() {
    for (godot::VisualInstance3D* sym : skullSymbols) {
        sym->set_layer_mask_value(1, true);
        if (auto* parent = godot::cast_to<godot::VisualInstance3D>(sym->get_parent())) {
            parent->set_layer_mask_value(1, false);
        }
    }
}

// gd: L188-L201
seq::Task HealthCounter::Bootup() {
    speaker_bootup->play();
    ClearDisplay();
    for (std::size_t i = 0; i < uiArray.size(); ++i) {
        uiArray[i]->set_visible(true);
    }
    co_await get_tree()->create_timer(.85);
    if (roundManager->playerData->currentBatchIndex == 2 && !settingUpBroken && !roundManager->endless) {
        speaker_beep->set_pitch_scale(.09f);
        SwapSkullSymbols();
        settingUpBroken = true;
    }
    speaker_beep->play();
    UpdateDisplay();
}

// gd: L203-L207
void HealthCounter::ClearDisplay() {
    for (std::size_t i = 0; i < symbolArray_player.size(); ++i) {
        symbolArray_player[i]->set_visible(false);
    }
    for (std::size_t i = 0; i < symbolArray_enemy.size(); ++i) {
        symbolArray_enemy[i]->set_visible(false);
    }
}

// gd: L209-L211
void HealthCounter::DisableCounter() {
    for (std::size_t i = 0; i < uiArray.size(); ++i) {
        uiArray[i]->set_visible(false);
    }
}

}  // namespace scripts
