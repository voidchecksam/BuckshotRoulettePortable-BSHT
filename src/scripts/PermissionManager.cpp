#include "scripts/PermissionManager.h"

#include "scripts/DescriptionManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/ItemInteraction.h"
#include "scripts/ItemManager.h"
#include "scripts/PickupIndicator.h"
#include "scripts/RoundManager.h"
#include "scripts/resources.h"

namespace scripts {

// gd: L12-L14
void PermissionManager::_ready() {
    // SetItemInteraction(true) is commented out in the .gd.
}

// gd: L16-L23
void PermissionManager::SetIndicators(bool state) {
    if (state) {
        for (PickupIndicator* i : indicatorArray) i->interactionAllowed = true;
    }
    if (!state) {
        for (PickupIndicator* i : indicatorArray) {
            i->interactionAllowed = false;
            i->moving = false;
        }
    }
}

// gd: L25-L33
void PermissionManager::DisableShotgun() {
    for (InteractionBranch* b : interactionBranchArray) {
        if (b->interactionAlias == "shotgun") {
            b->interactionAllowed = false;
            break;
        }
    }
    for (PickupIndicator* i : indicatorArray) {
        if (i->itemName == "SHOTGUN") {
            i->interactionAllowed = false;
            break;
        }
    }
}

// gd: L36-L51
void PermissionManager::SetInteractionPermissions(bool state) {
    if (state) {
        for (InteractionBranch* b : interactionBranchArray) b->interactionAllowed = true;
    }
    if (!state) {
        for (InteractionBranch* b : interactionBranchArray) b->interactionAllowed = false;
    }
    if (roundManager->roundArray[roundManager->currentRound]->usingItems &&
        !itemManager->items_dynamicIndicatorArray.empty()) {
        for (size_t i = 0; i < itemManager->items_dynamicInteractionArray.size(); i++) {
            itemManager->items_dynamicInteractionArray[i]->interactionAllowed = state;
            itemManager->items_dynamicIndicatorArray[i]->interactionAllowed = state;
        }
    }
}

// gd: L53-L62
void PermissionManager::SetItemInteraction(bool state) {
    godot::Node* parent = userItemsParent;
    for (int i = 0; i < parent->get_child_count(); i++) {
        godot::Node* child = parent->get_child(i);
        if (child->get_child_count() > 0) {   // get_child(0) != null
            if (auto* tempindicator = godot::script_cast<PickupIndicator>(child->get_child(0))) {
                tempindicator->interactionAllowed = state;
            }
            if (child->get_child_count() > 1) {
                if (auto* tempBranch = godot::script_cast<InteractionBranch>(child->get_child(1))) {
                    tempBranch->interactionAllowed = state;
                }
            }
        }
    }
}

// gd: L65-L80
void PermissionManager::SetStackInvalidIndicators() {
    stackDisabledItemArray_bools[5] = inter->stealing;
    stackDisabledItemArray_bools[4] = roundManager->dealerCuffed;
    stackDisabledItemArray_bools[0] = roundManager->barrelSawedOff;
    if (roundManager->roundArray[roundManager->currentRound]->usingItems &&
        !itemManager->items_dynamicIndicatorArray.empty()) {
        for (size_t i = 0; i < stackDisabledItemArray.size(); i++) {
            for (size_t c = 0; c < itemManager->items_dynamicInteractionArray.size(); c++) {
                InteractionBranch* branch = itemManager->items_dynamicInteractionArray[c];
                PickupIndicator* indicator = itemManager->items_dynamicIndicatorArray[c];
                if (branch->itemName == stackDisabledItemArray[i] && stackDisabledItemArray_bools[i]) {
                    branch->interactionInvalid = true;
                    indicator->interactionInvalid = true;
                }
                if (branch->itemName == stackDisabledItemArray[i] && !stackDisabledItemArray_bools[i]) {
                    branch->interactionInvalid = false;
                    indicator->interactionInvalid = false;
                }
            }
        }
    }
}

// gd: L82-L83
void PermissionManager::RevertDescriptionUI() {
    description->EndLerp();
}

}  // namespace scripts
