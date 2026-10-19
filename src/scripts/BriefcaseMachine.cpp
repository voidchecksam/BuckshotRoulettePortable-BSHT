#include "scripts/BriefcaseMachine.h"

#include "godot/scene_tree.h"
#include "scripts/CameraManager.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/EndingManager.h"
#include "scripts/InteractionBranch.h"

namespace scripts {

// gd: L26-L42
seq::Task BriefcaseMachine::MainRoutine() {
    speaker_machine->play();
    co_await get_tree()->create_timer(.42);
    parent_eyes->set_visible(true);
    co_await get_tree()->create_timer(1.28);
    parent_eyes->set_visible(false);
    anim_machine->play("move to table");
    parent_machine->set_visible(true);
    co_await get_tree()->create_timer(5.14);
    cam->BeginLerp("cash briefcase");
    co_await get_tree()->create_timer(.8);
    cursor->SetCursor(true, true);
    intbranch_L->interactionAllowed = true;
    intbranch_R->interactionAllowed = true;
    btnParent_briefcase->set_visible(true);
    if (cursor->controller_active) btn_left->grab_focus();
    controller->previousFocus = btn_left;
}

// gd: L44-L47
seq::Task BriefcaseMachine::CheckLatches() {
    if (latchRaisedL == true && latchRaisedR == true) {
        co_await get_tree()->create_timer(.3);
        intbranch_lid->interactionAllowed = true;
    }
}

// gd: L49-L62
void BriefcaseMachine::OpenLatch(const std::string& alias) {
    if (alias == "L") {
        intbranch_L->interactionAllowed = false;
        speaker_latchL->play();
        anim_latchL->play("raise");
        latchRaisedL = true;
        get_tree()->spawn(CheckLatches(), get_node_owner());
    } else if (alias == "R") {
        intbranch_R->interactionAllowed = false;
        speaker_latchR->play();
        anim_latchR->play("raise");
        latchRaisedR = true;
        get_tree()->spawn(CheckLatches(), get_node_owner());
    }
}

// gd: L64-L70
void BriefcaseMachine::OpenLid() {
    btnParent_briefcase->set_visible(false);
    cursor->SetCursor(false, false);
    intbranch_lid->interactionAllowed = false;
    anim_lid->play("open");
    speaker_lid->play();
    ending->BeginEnding();
}

}  // namespace scripts
