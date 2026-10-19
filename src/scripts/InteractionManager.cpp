#include "scripts/InteractionManager.h"

#include "scripts/BriefcaseMachine.h"
#include "scripts/CrtManager.h"
#include "scripts/HeavenManager.h"

#include "scripts/IntroManager.h"
#include "scripts/SignatureButtonBranch.h"
#include "scripts/SignatureManager.h"
#include "scripts/ItemInteraction.h"
#include "scripts/ItemManager.h"

#include "godot/scene_tree.h"
#include "scripts/CursorManager.h"
#include "scripts/DecisionTextManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/MouseRaycast.h"
#include "scripts/PickupIndicator.h"
#include "scripts/RoundManager.h"
#include "scripts/ShotgunShooting.h"

namespace scripts {

// gd: L18-L22
void InteractionManager::_process(double /*delta*/) {
    CheckPickupLerp();
    CheckInteractionBranch();
    if (checking) CheckIfHovering();
}

// gd: L24-L26
void InteractionManager::_input(const godot::InputEventRef& event) {
    const auto* mb = godot::event_cast<godot::InputEventMouseButton>(event.get());
    if (mb && mb->button_index == godot::mouse::LEFT && mb->is_pressed()) MainInteractionEvent();
}

// gd: L28-L33
void InteractionManager::MainInteractionEvent() {
    if (activeInteractionBranch != nullptr && activeInteractionBranch->interactionAllowed &&
        !activeInteractionBranch->interactionInvalid) {
        godot::Node* parent = activeInteractionBranch->get_node_owner()->get_parent();
        for (int i = 0; parent && i < parent->get_child_count(); i++) {
            if (auto* indicator = godot::script_cast<PickupIndicator>(parent->get_child(i))) indicator->SnapToMax();
        }
        if (!activeInteractionBranch->isGrid) {
            InteractWith(activeInteractionBranch->interactionAlias);
        } else {
            InteractWithGrid(activeInteractionBranch->gridIndex);
        }
    }
}

// gd: L35-L42
void InteractionManager::CheckIfHovering() {
    if (activeInteractionBranch != nullptr && activeInteractionBranch->interactionAllowed) {
        if (activeInteractionBranch->interactionAllowed && !activeInteractionBranch->interactionInvalid) {
            cursor->SetCursorImage("hover");
        } else if (activeInteractionBranch->interactionAllowed && activeInteractionBranch->interactionInvalid) {
            cursor->SetCursorImage("invalid");
        }
    } else {
        cursor->SetCursorImage("point");
    }
}

// gd: L45-L96
void InteractionManager::InteractWith(const std::string& alias) {
    port_interactions.push_back(alias);
    godot::SceneTree* tree = get_tree();
    if (alias == "shotgun") {
        tree->spawn(shotgun->GrabShotgun());
    } else if (alias == "text dealer") {
        if (!fs_dec) {
            tree->spawn(shotgun->Shoot("dealer"));
            decision->SetUI(false);
            fs_dec = true;
        }
    } else if (alias == "text you") {
        if (!fs_dec) {
            tree->spawn(shotgun->Shoot("self"));
            decision->SetUI(false);
            fs_dec = true;
        }
    } else if (alias == "briefcase intake") {
        // gd: itemManager.GrabItem(), called without await: spawned.
        tree->spawn(itemManager->GrabItem());
    } else if (alias == "item") {
        // gd: itemInteraction.PickupItemFromTable(...), called without await: spawned.
        if (activeInteractionBranch->itemName != "") {
            tree->spawn(itemInteraction->PickupItemFromTable(
                godot::cast_to<godot::Node3D>(activeInteractionBranch->get_node_owner()->get_parent()),
                activeInteractionBranch->itemName));
        }
    } else if (alias == "bathroom door") {
        // gd: intro.Interaction_BathroomDoor(), called without await: spawned, owned by the intro.
        tree->spawn(intro->Interaction_BathroomDoor(), intro->get_node_owner());
    } else if (alias == "backroom door") {
        intro->Interaction_BackroomDoor();
    } else if (alias == "heaven door") {
        // gd: heaven.Fly(), not awaited: spawned, owned by the heaven manager.
        tree->spawn(heaven->Fly(), heaven->get_node_owner());
    } else if (alias == "latch left") {
        brief->OpenLatch("L");
    } else if (alias == "latch right") {
        brief->OpenLatch("R");
    } else if (alias == "briefcase lid") {
        brief->OpenLid();
    } else if (alias == "release form on table") {
        tree->spawn(sign->PickUpWaiver(), sign->get_node_owner());
    } else if (alias == "signature machine button") {
        sign->GetInput(activeInteractionBranch->signatureButton_letterAlias,
                       activeInteractionBranch->signatureButton_specialAlias);
        SignButton* pressed = activeInteractionBranch->assignedSignatureButton;
        if (pressed) tree->spawn(pressed->Press(), pressed->get_node_owner());
    } else if (alias == "pill bottle") {
        tree->spawn(intro->Interaction_PillBottle(), intro->get_node_owner());
    } else if (alias == "pill choice no") {
        tree->spawn(intro->SelectedPill(false), intro->get_node_owner());
    } else if (alias == "pill choice yes") {
        tree->spawn(intro->SelectedPill(true), intro->get_node_owner());
    } else if (alias == "double yes") {
        RoundManager* rm = shotgun->roundManager;
        tree->spawn(rm->Response(true), rm->get_node_owner());
    } else if (alias == "double no") {
        RoundManager* rm = shotgun->roundManager;
        tree->spawn(rm->Response(false), rm->get_node_owner());
    } else if (alias == "crt") {
        tree->spawn(intro->Interaction_CRT(), intro->get_node_owner());
    } else if (alias == "crt button") {
        if (activeInteractionBranch->crtButton_alias != "") {
            // gd: #activeInteractionBranch.get_parent().get_child(1).Press()
            tree->spawn(crt->Interaction(activeInteractionBranch->crtButton_alias), crt->get_node_owner());
        }
    }
}

// gd: L98-L101
void InteractionManager::SignatureButtonRemote(SignButton* assignedBranch, const std::string& alias) {
    sign->GetInput(alias, alias);
    if (assignedBranch) get_tree()->spawn(assignedBranch->Press(), assignedBranch->get_node_owner());
}

// gd: L103-L105
void InteractionManager::InteractWithGrid(int tempGridIndex) {
    // gd: itemManager.PlaceDownItem(tempGridIndex), called without await: spawned.
    get_tree()->spawn(itemManager->PlaceDownItem(tempGridIndex));
}

// gd: L107-L118
void InteractionManager::CheckPickupLerp() {
    if (mouseRay->result.has_value() && mouseRay->result->collider && cursor->cursor_visible) {
        godot::Node* parent = mouseRay->result->collider->get_parent();
        activeParent = parent;
        for (int i = 0; i < parent->get_child_count(); i++) {
            if (auto* indicator = godot::script_cast<PickupIndicator>(parent->get_child(i))) {
                (void)indicator;   // the .gd assigns it to a local and never uses it
            }
        }
    } else {
        activeParent = nullptr;
    }
}

// gd: L120-L133
void InteractionManager::CheckInteractionBranch() {
    InteractionBranch* isFound = nullptr;
    if (activeParent != nullptr) {
        for (int i = 0; i < activeParent->get_child_count(); i++) {
            if (auto* branch = godot::script_cast<InteractionBranch>(activeParent->get_child(i))) {
                isFound = branch;
                break;
            }
        }
    }
    if (isFound) {
        activeInteractionBranch = isFound;
    } else {
        activeInteractionBranch = nullptr;
    }
}

}  // namespace scripts
