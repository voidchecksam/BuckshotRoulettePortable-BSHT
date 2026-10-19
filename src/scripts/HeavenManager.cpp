#include "scripts/HeavenManager.h"

#include "godot/print.h"
#include "godot/scene_tree.h"
#include "scripts/AchievementManager.h"
#include "scripts/ButtonClass.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/GlobalVariables.h"
#include "scripts/InteractionBranch.h"
#include "scripts/InteractionManager.h"

namespace scripts {

// gd: L15-L20
void Heaven::_ready() {
    get_tree()->spawn(ReadyRoutine(), get_node_owner());
}

seq::Task Heaven::ReadyRoutine() {
    Signals();
    get_tree()->spawn(BeginLoop(), get_node_owner());
    CheckControllerState();
    co_await get_tree()->create_timer(1);
    ach->UnlockAchievement("ach2");
}

// gd: L22-L25
void Heaven::Signals() {
    buttonArray[0]->get_node_owner()->connect("is_pressed", [this] { Button_Retry(); });
    buttonArray[1]->get_node_owner()->connect("is_pressed", [this] { Button_Exit(); });
}

// gd: L27-L29
void Heaven::CheckControllerState() {
    if (GlobalVariables::controllerEnabled) controller->SetMainControllerState(true);
}

// gd: L31-L32
void Heaven::Button_Retry() {
    get_tree()->spawn(Button_Main("retry"), get_node_owner());
}

// gd: L34-L35
void Heaven::Button_Exit() {
    get_tree()->spawn(Button_Main("exit"), get_node_owner());
}

// gd: L37-L51
seq::Task Heaven::Button_Main(std::string tag) {
    speaker_music->stop();
    speaker_button->play();
    cursor->SetCursor(false, false);
    animator->play("fade out");
    for (godot::Control* u : ui) u->set_visible(false);
    for (ButtonClass* cl : buttonArray) cl->isActive = false;
    co_await get_tree()->create_timer(3.12);
    if (tag == "retry") {
        godot::print("changing scene to: death");
        get_tree()->change_scene_to_file("res://scenes/death.tscn");
    } else if (tag == "exit") {
        get_tree()->quit();
    }
}

// gd: L57-L68
seq::Task Heaven::BeginLoop() {
    co_await get_tree()->create_timer(1);
    speaker_music->play();
    viewblocker->set_visible(false);
    animator->play("camera pan down");
    animator->queue("camera idle");
    co_await get_tree()->create_timer(11.1);
    if (cursor->controller_active) btn_door->grab_focus();
    controller->previousFocus = btn_door;
    btn_door->set_visible(true);
    cursor->SetCursor(true, true);
    intbranch_heavendoor->interactionAllowed = true;
}

// gd: L70-L84
seq::Task Heaven::Fly() {
    btn_door->set_visible(false);
    bracket_door->set_visible(false);
    bracket_door->set_global_position({0, 0, -100});
    intbranch_heavendoor->interactionAllowed = false;
    cursor->SetCursor(false, false);
    animator->play("move");
    co_await get_tree()->create_timer(15.6);
    cursor->SetCursor(true, true);
    if (cursor->controller_active) btn_retry->grab_focus();
    controller->previousFocus = btn_retry;
    interactionManager->checking = false;
    for (ButtonClass* cl : buttonArray) cl->isActive = true;
}

}  // namespace scripts
