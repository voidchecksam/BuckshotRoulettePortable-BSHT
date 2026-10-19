#include "scripts/ShellSpawner.h"

#include "godot/array_ops.h"
#include "godot/packed_scene.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/CameraManager.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/DialogueManager.h"
#include "scripts/HealthCounter.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellBranch.h"
#include "scripts/resources.h"

#include <memory>

namespace scripts {

// gd: L24-L25
void ShellSpawner::_ready() {}

// gd: L28-L29
void ShellSpawner::_process(double /*delta*/) {}

// gd: L32-L94
seq::Task ShellSpawner::MainShellRoutine() {
    if (roundManager->playerData->currentBatchIndex != 0) {
        roundManager->shellLoadingSpedUp = true;
    }
    //CLEAR PREVIOUS SHELL SEQUENCE ARRAY
    sequenceArray.clear();

    //INITIAL CAMERA SOCKET. HEALTH COUNTER FUNCTIONALITY
    if (roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)]->bootingUpCounter) {
        camera->BeginLerp("health counter");
        co_await get_tree()->create_timer(.5);
        get_tree()->spawn(healthCounter->Bootup());
        co_await get_tree()->create_timer(1.4);
    }
    camera->BeginLerp("shell compartment");
    co_await get_tree()->create_timer(.5);
    //SHELL SPAWNING
    const RoundClass& round = *roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)];
    const int temp_nr = round.amountBlank + round.amountLive;
    const int temp_live = round.amountLive;
    const int temp_blank = round.amountBlank;
    const bool temp_shuf = round.shufflingArray;
    SpawnShells(temp_nr, temp_live, temp_blank, temp_shuf);
    seq = sequenceArray;
    anim_compartment->play("show shells");
    PlayLatchSound();
    get_tree()->spawn(PlayAudioIndicators());
    co_await get_tree()->create_timer(1);
    roundManager->ignoring = false;
    //DIALOGUE
    std::string text_lives;
    std::string text_blanks;
    if (temp_live == 1) {
        text_lives = godot::format(godot::tr("LIVEROUND"), {godot::str(temp_live)});
    } else {
        text_lives = godot::format(godot::tr("LIVEROUNDS"), {godot::str(temp_live)});
    }
    if (temp_blank == 1) {
        text_blanks = godot::format(godot::tr("BLANKROUND"), {godot::str(temp_blank)});
    } else {
        text_blanks = godot::format(godot::tr("BLANKROUNDS"), {godot::str(temp_blank)});
    }
    const std::string finalstring = text_lives + " " + text_blanks;
    double maindur = 1.3;
    if (roundManager->playerData->currentBatchIndex == 2) {
        roundManager->playerData->skippingShellDescription = true;
    }
    if (!roundManager->playerData->skippingShellDescription) {
        if (CheckBackdropScaling()) dialogue->scaling = true;
        dialogue->ShowText_Forever(finalstring);
    }
    if (roundManager->playerData->skippingShellDescription && !skipDialoguePresented) {
        dialogue->ShowText_Forever(godot::tr("DRILL"));
        maindur = 2.5;
        skipDialoguePresented = true;
    }
    if (!roundManager->playerData->skippingShellDescription) {
        co_await get_tree()->create_timer(2.5);
    } else {
        co_await get_tree()->create_timer(maindur);
    }
    dialogue->HideText();
    //HIDE SHELLS
    anim_compartment->play("hide shells");
    PlayLatchSound();
    if (roundManager->shellLoadingSpedUp) {
        co_await get_tree()->create_timer(.2);
    } else {
        co_await get_tree()->create_timer(.5);
    }
    //CHECK IF INSERTING INTO CHAMBER IN RANDOM ORDER.
    if (roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)]->insertingInRandomOrder) {
        godot::shuffle(sequenceArray);
        godot::shuffle(sequenceArray);
    }
    roundManager->LoadShells();
}

// gd: L96-L102
bool ShellSpawner::CheckBackdropScaling() {
    // PORT: the locale is not read. It only widens the dialogue backdrop,
    // which the port does not scale (GameApp::SyncDialogueBox).
    const std::string curloc = "EN";
    const std::vector<std::string> localesToScale = {"FR", "IT", "DE", "ES", "ES LATAM", "BR", "PT"};
    for (const std::string& l : localesToScale) {
        if (curloc == l) {
            return true;
        }
    }
    return false;
}

// gd: L104-L139
void ShellSpawner::SpawnShells(int numberOfShells, int numberOfLives, int numberOfBlanks, bool shufflingArray) {
    //DELETE PREVIOUS SHELLS
    for (std::size_t i = 0; i < spawnedShellObjectArray.size(); ++i) {
        spawnedShellObjectArray[i]->queue_free();
    }
    spawnedShellObjectArray.clear();

    //SETUP SHELL ARRAY
    sequenceArray.clear();
    tempSequence.clear();
    ai->sequenceArray_knownShell.clear();
    for (int i = 0; i < numberOfLives; ++i) {
        tempSequence.push_back("live");
    }
    for (int i = 0; i < numberOfBlanks; ++i) {
        tempSequence.push_back("blank");
    }
    if (shufflingArray) {
        godot::shuffle(tempSequence);
    }
    for (std::size_t i = 0; i < tempSequence.size(); ++i) {
        sequenceArray.push_back(tempSequence[i]);
        ai->sequenceArray_knownShell.push_back(false);
    }

    locationIndex = 0;
    //SPAWN SHELLS
    for (int i = 0; i < numberOfShells; ++i) {
        std::unique_ptr<godot::Node> instance = shellInstance->instantiate();
        spawnedShell = godot::cast_to<godot::Node3D>(instance.get());
        shellBranch = godot::script_cast<ShellClass>(spawnedShell->get_child(0));
        if (sequenceArray[static_cast<std::size_t>(i)] == "live") {
            shellBranch->isLive = true;
        } else {
            shellBranch->isLive = false;
        }
        shellBranch->ApplyStatus();
        // PORT: add_child takes ownership of the instance; spawnedShell keeps
        // pointing at it, as the GDScript variable does.
        spawnParent->add_child(std::move(instance));
        spawnedShell->set_transform_origin(shellLocationArray[static_cast<std::size_t>(locationIndex)]);
        spawnedShell->set_rotation_degrees({-90.0f, -90.0f, 180.0f});
        spawnedShellObjectArray.push_back(spawnedShell);
        locationIndex += 1;
    }
}

// gd: L142-L146
void ShellSpawner::PlayLatchSound() {
    tog = !tog;
    if (tog) {
        speaker_latchOpen->set_stream(soundArray_latchOpen[0]);
    } else {
        speaker_latchOpen->set_stream(soundArray_latchOpen[1]);
    }
    speaker_latchOpen->play();
}

// gd: L149-L155
seq::Task ShellSpawner::PlayAudioIndicators() {
    co_await get_tree()->create_timer(.37);
    for (std::size_t i = 0; i < seq.size(); ++i) {
        if (seq[i] == "blank") {
            speaker_audioIndicator->set_stream(soundArray_indicators[0]);
        } else {
            speaker_audioIndicator->set_stream(soundArray_indicators[1]);
        }
        speaker_audioIndicator->play();
        co_await get_tree()->create_timer(.07);
    }
}

}  // namespace scripts
