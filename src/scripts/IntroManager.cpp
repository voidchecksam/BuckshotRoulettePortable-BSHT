#include "scripts/IntroManager.h"

#include "godot/array_ops.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/AchievementManager.h"
#include "scripts/BlockoutManager.h"
#include "scripts/BpmLight.h"
#include "scripts/CameraShaker.h"
#include "scripts/ControllerManager.h"
#include "scripts/CrtManager.h"
#include "scripts/CursorManager.h"
#include "scripts/DialogueManager.h"
#include "scripts/DonUnlockManager.h"
#include "scripts/CrtManager.h"
#include "scripts/EndlessMode.h"
#include "scripts/FilterController.h"
#include "scripts/GlobalVariables.h"
#include "scripts/PipelineManager.h"
#include "scripts/StatueManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/MusicManager.h"
#include "scripts/RoundManager.h"
#include "scripts/resources.h"

namespace scripts {

void IntroManager::Spawn(seq::Task&& task) {
    get_tree()->spawn(std::move(task), get_node_owner());
}

// gd: L47-L63
void IntroManager::_ready() {
    Spawn(ReadyRoutine());
}

seq::Task IntroManager::ReadyRoutine() {
    parent_pills->set_visible(false);
    allowingPills = false;
    SetControllerState();
    co_await get_tree()->create_timer(.5);
    PlayerData* pd = roundManager->playerData;
    if (pd->playerEnteringFromDeath && !pd->enteringFromTrueDeath) {
        Spawn(RevivalBathroomStart());
    } else {
        Spawn(MainBathroomStart());
    }
    if (pd->playerEnteringFromDeath || pd->enteringFromTrueDeath) {
        parent_pills->set_visible(false);
        allowingPills = false;
    }
    if (!pd->playerEnteringFromDeath && !pd->enteringFromTrueDeath) {
        if (unlocker && unlocker->Unlocked()) {   // gd: FileAccess.file_exists(unlocker.savepath)
            parent_pills->set_visible(true);
            crtManager->SetCRT(true);
            allowingPills = true;
        }
    }
}

// gd: L69-L70
void IntroManager::_process(double /*delta*/) {
    if (counting) CountTimer();
}

// gd: L72-L76
void IntroManager::CountTimer() {
    if (counting) count_current += get_process_delta_time();
    if (count_current > count_max && !fs1) {
        if (ach) ach->UnlockAchievement("ach14");
        fs1 = true;
    }
}

// gd: L78-L80
void IntroManager::SetControllerState() {
    if (GlobalVariables::controllerEnabled) controller->SetMainControllerState(true);
}

// gd: L82-L100
seq::Task IntroManager::MainBathroomStart() {
    RestRoomIdle();
    co_await get_tree()->create_timer(.5);
    viewblocker->set_visible(false);
    MainTrackLoad();
    co_await get_tree()->create_timer(3);
    Spawn(Hint());
    cursor->SetCursor(true, true);
    intbranch_bathroomdoor->interactionAllowed = true;
    if (allowingPills) {
        intbranch_pillbottle->interactionAllowed = true;
        if (GlobalVariables::using_steam) intbranch_crt->interactionAllowed = true;
    }
    if (cursor->controller_active) btn_bathroomdoor->grab_focus();
    controller->previousFocus = btn_bathroomdoor;
    if (allowingPills) {
        btn_pills->set_visible(true);
        if (GlobalVariables::using_steam) btn_screen->set_visible(true);
    }
    btn_bathroomdoor->set_visible(true);
    anim_pillflicker->play("flicker pill");
}

// gd: L102-L106
seq::Task IntroManager::Hint() {
    co_await get_tree()->create_timer(1);
    if (!roundManager->playerData->seenHint) {
        animator_hint->play("show");
        roundManager->playerData->seenHint = true;
    }
}

// gd: L108-L153
seq::Task IntroManager::RevivalBathroomStart() {
    cameraParent->set_transform_origin(pos_floor);
    cameraParent->set_rotation_degrees(rot_floor);
    // gd: #animator_smokerdude.stop(true), commented out in the .gd
    smokerdude_revival->set_visible(true);
    co_await get_tree()->create_timer(1);
    speaker_defib->play();
    co_await get_tree()->create_timer(.85);
    dia->speaker_click->set_stream(dia->soundArray_clicks[0]);
    animator_smokerdude->play("revive player");
    cameraShaker->Shake();
    animator_pp->play("revival brightness");
    viewblocker->set_visible(false);
    MainTrackLoad();
    co_await get_tree()->create_timer(.5);
    animator_smokerdude->play("revive player");
    dia->ShowText_Forever(godot::tr("YOURE LUCKY"));
    const std::string n = roundManager->playerData->playername;
    const std::string firstpart = godot::format_s(godot::tr("GET UP"), n);
    const std::string secondpart = "\n" + godot::tr("THE NIGHT");
    const std::string full = firstpart + secondpart;
    co_await get_tree()->create_timer(4);
    dia->ShowText_Forever(full);
    co_await get_tree()->create_timer(4);
    dia->HideText();
    animator_pp->play("brightness fade out");
    co_await get_tree()->create_timer(2.05);
    smokerdude_revival->set_visible(false);
    RestRoomIdle();
    animator_pp->play("brightness fade in");
    dia->speaker_click->set_stream(dia->soundArray_clicks[3]);
    co_await get_tree()->create_timer(3);
    cursor->SetCursor(true, true);
    if (allowingPills) {
        intbranch_pillbottle->interactionAllowed = true;
        if (GlobalVariables::using_steam) {
            intbranch_crt->interactionAllowed = true;
            btn_screen->set_visible(true);
        }
        btn_pills->set_visible(true);
        anim_pillflicker->play("flicker pill");
    }
    if (cursor->controller_active) btn_bathroomdoor->grab_focus();
    controller->previousFocus = btn_bathroomdoor;
    btn_bathroomdoor->set_visible(true);
    intbranch_bathroomdoor->interactionAllowed = true;
}

// gd: L155-L160
void IntroManager::MainTrackLoad() {
    const double increment =
        musicmanager->trackArray[static_cast<std::size_t>(roundManager->playerData->currentBatchIndex)]->bpmIncrement;
    bpmlight->delay = increment;
    musicmanager->LoadTrack();
    bpmlight->BeginMainLoop();
    speaker_amb_restroom->play();
}

// gd: L162-L185
seq::Task IntroManager::Interaction_PillBottle() {
    anim_pillflicker->play("RESET");
    cursor->SetCursor(false, false);
    intbranch_bathroomdoor->interactionAllowed = false;
    intbranch_pillbottle->interactionAllowed = false;
    intbranch_crt->interactionAllowed = false;
    btn_pills->set_visible(false);
    btn_screen->set_visible(false);
    btn_bathroomdoor->set_visible(false);
    animator_camera->play("camera check pills");
    co_await get_tree()->create_timer(.6);
    speaker_pillchoice->play();
    co_await get_tree()->create_timer(.3);
    animator_pillchoice->play("show");
    co_await get_tree()->create_timer(.7);
    cursor->SetCursor(true, true);

    if (cursor->controller_active) btn_pillsNo->grab_focus();
    controller->previousFocus = btn_pillsNo;
    for (godot::CollisionShape3D* c : col_pillchoice) {
        if (c) c->disabled = false;
    }
    btn_pillsNo->set_visible(true);
    btn_pillsYes->set_visible(true);
    intbranch_pillyes->interactionAllowed = true;
    intbranch_pillno->interactionAllowed = true;
}

// gd: L188-L201
seq::Task IntroManager::Interaction_CRT() {
    Spawn(StartSound());
    anim_pillflicker->play("RESET");
    cursor->SetCursor(false, false);
    intbranch_bathroomdoor->interactionAllowed = false;
    intbranch_pillbottle->interactionAllowed = false;
    intbranch_crt->interactionAllowed = false;
    btn_pills->set_visible(false);
    btn_bathroomdoor->set_visible(false);
    btn_screen->set_visible(false);
    animator_camera->play("camera check crt");
    co_await get_tree()->create_timer(2.6);
    // gd: crtManager.Bootup(), not awaited: spawned, owned by the CRT manager.
    get_tree()->spawn(crtManager->Bootup(), crtManager->get_node_owner());
    co_await get_tree()->create_timer(0.54);
}

// gd: L203-L206
seq::Task IntroManager::StartSound() {
    crtManager->speaker_playerwalk->play();
    co_await get_tree()->create_timer(2.04);
    crtManager->speaker_bootuploop->play();
}

// gd: L208-L210
void IntroManager::EnabledInteractionCRT() {
    cursor->SetCursor(true, true);
    for (InteractionBranch* b : intbs_crtbuttons) {
        if (b) b->interactionAllowed = true;
    }
}

// gd: L212-L214
void IntroManager::DisableInteractionCrt() {
    cursor->SetCursor(false, false);
    for (InteractionBranch* b : intbs_crtbuttons) {
        if (b) b->interactionAllowed = false;
    }
}

// gd: L218-L253
seq::Task IntroManager::SelectedPill(bool selected) {
    if (selected) {
        if (pill_unlock) pill_unlock->IncrementAmount();
        anim_pillflicker->play("RESET");
    }
    cursor->SetCursor(false, false);
    for (godot::CollisionShape3D* c : col_pillchoice) {
        if (c) c->disabled = true;
    }
    animator_pp->play("brightness fade out");
    anim_revert->play("revert");
    animator_pillchoice->play("hide");
    speaker_pillchoice->stop();
    speaker_pillselect->play();
    btn_pillsYes->set_visible(false);
    btn_pillsNo->set_visible(false);
    intbranch_pillyes->interactionAllowed = false;
    intbranch_pillno->interactionAllowed = false;
    co_await get_tree()->create_timer(2.05);
    if (selected) {
        parent_pills->set_visible(false);
        endlessmode->SetupEndless();
    }
    RestRoomIdle();
    if (selected) crtManager->SetCRT(false);
    animator_pp->play("brightness fade in");
    co_await get_tree()->create_timer(.6);
    cursor->SetCursor(true, true);
    if (selected && ach) ach->UnlockAchievement("ach3");
    intbranch_bathroomdoor->interactionAllowed = true;
    btn_bathroomdoor->set_visible(true);
    if (!selected) {
        intbranch_pillbottle->interactionAllowed = true;
        intbranch_crt->interactionAllowed = true;
        btn_pills->set_visible(true);
        btn_screen->set_visible(true);
        anim_pillflicker->play("flicker pill");
    }
    if (cursor->controller_active) btn_bathroomdoor->grab_focus();
    controller->previousFocus = btn_bathroomdoor;
}

// gd: L255-L275
seq::Task IntroManager::RevertCRT() {
    animator_pp->play("brightness fade out");
    // gd: #anim_revert.play("revert"), commented out in the .gd
    btn_pillsYes->set_visible(false);
    btn_pillsNo->set_visible(false);
    intbranch_pillyes->interactionAllowed = false;
    intbranch_pillno->interactionAllowed = false;
    co_await get_tree()->create_timer(2.05);
    RestRoomIdle();
    animator_pp->play("brightness fade in");
    anim_pillflicker->play("flicker pill");
    co_await get_tree()->create_timer(.6);
    cursor->SetCursor(true, true);
    intbranch_bathroomdoor->interactionAllowed = true;
    btn_bathroomdoor->set_visible(true);
    intbranch_pillbottle->interactionAllowed = true;
    intbranch_crt->interactionAllowed = true;
    btn_pills->set_visible(true);
    btn_screen->set_visible(true);
    if (cursor->controller_active) btn_bathroomdoor->grab_focus();
    controller->previousFocus = btn_bathroomdoor;
}

// gd: L278-L285
void IntroManager::Interaction_BackroomDoor() {
    statue->CheckStatus();
    roundManager->playerData->stat_doorsKicked += 1;
    animator_camera->play("camera enter backroom");
    intbranch_backroomdoor->interactionAllowed = false;
    btn_backroom->set_visible(false);
    cursor->SetCursor(false, false);
    counting = false;
}

// gd: L287-L303
seq::Task IntroManager::Interaction_BathroomDoor() {
    intbranch_bathroomdoor->interactionAllowed = false;
    cursor->SetCursor(false, false);
    btn_pills->set_visible(false);
    btn_screen->set_visible(false);
    btn_bathroomdoor->set_visible(false);
    animator_camera->play("camera exit bathroom");
    co_await get_tree()->create_timer(5);
    cursor->SetCursor(true, true);
    if (cursor->controller_active) btn_backroom->grab_focus();
    controller->previousFocus = btn_backroom;
    btn_backroom->set_visible(true);
    intbranch_backroomdoor->interactionAllowed = true;
    intbranch_pillbottle->interactionAllowed = false;
    intbranch_crt->interactionAllowed = false;
    counting = true;
}

// gd: L306-L308
void IntroManager::BeginGame() {
    blockout->HideClub();
    roundManager->BeginMainGame();
}

// gd: L310-L312
void IntroManager::PanFilter() {
    filter->BeginPan(filter->lowPassMaxValue, filter->lowPassDefaultValue);
}

// gd: L314-L315
void IntroManager::KickDoorBackroom() {}

// gd: L317-L321
seq::Task IntroManager::KickDoorLobby() {
    roundManager->playerData->stat_doorsKicked += 1;
    co_await get_tree()->create_timer(.25);
    filter->BeginPan(filter->lowPassDefaultValue, filter->lowPassMaxValue);
    speaker_amb_restroom->stop();
}

// gd: L324-L327
void IntroManager::RestRoomIdle() {
    pipeline->AdjustSettings(pipeline->scene);
    animator_camera->play("camera idle bathroom");
}

bool IntroManager::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "BeginGame") {
        BeginGame();
    } else if (method == "PanFilter") {
        PanFilter();
    } else if (method == "KickDoorLobby") {
        Spawn(KickDoorLobby());
    } else if (method == "KickDoorBackroom") {
        KickDoorBackroom();
    } else {
        return false;
    }
    return true;
}

}  // namespace scripts
