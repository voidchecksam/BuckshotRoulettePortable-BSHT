#include "scripts/HandManager.h"

#include "godot/math_funcs.h"
#include "godot/scene_tree.h"
#include "scripts/CameraManager.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/InteractionBranch.h"
#include "scripts/ItemAmounts.h"
#include "scripts/ItemInteraction.h"
#include "scripts/ItemManager.h"
#include "scripts/PickupIndicator.h"

#include <algorithm>
#include <cctype>

namespace scripts {

namespace {

// Array.erase: the FIRST occurrence only.
template <class T>
void EraseFirst(std::vector<T>& v, const T& value) {
    const auto it = std::find(v.begin(), v.end(), value);
    if (it != v.end()) v.erase(it);
}

}  // namespace

// gd: L48-L49
void HandManager::_process(double /*delta*/) {
    LerpHandMovement();
}

// gd: L52-L129
seq::Task HandManager::PickupItemFromTable(std::string itemName) {
    dealerAI->Speaker_HandCrack();
    godot::Node3D* activeInstance = nullptr;
    std::size_t matchIndex = 0;
    activeItemToGrab = itemName;
    for (std::size_t i = 0; i < itemManager->itemArray_instances_dealer.size(); ++i) {
        auto* b = godot::script_cast<InteractionBranch>(itemManager->itemArray_instances_dealer[i]->get_child(1));
        if (itemName == b->itemName) {
            activeInstance = itemManager->itemArray_instances_dealer[i];
            matchIndex = i;
            break;
        }
    }
    auto* indicator = godot::script_cast<PickupIndicator>(activeInstance->get_child(0));
    const int activeIndex = indicator->dealerGridIndex;
    ToggleHandVisible("BOTH", false);
    hand_defaultL->set_visible(true);
    hand_defaultR->set_visible(true);
    std::string whichHandToGrabWith;
    if (itemName == "beer" || itemName == "cigarettes" || itemName == "expired medicine" || itemName == "inverter" ||
        itemName == "adrenaline")
        whichHandToGrabWith = "left";
    else
        whichHandToGrabWith = "right";
    const std::string whichGridSide = indicator->whichSide;
    animator_hands->play("RESET");
    BeginHandLerp(whichHandToGrabWith, activeIndex, whichGridSide);
    if (whichGridSide == "right") animator_dealerHeadLook->play("dealer look right");
    else animator_dealerHeadLook->play("dealer look left");
    if (stealing) {
        if (whichGridSide == "right") cam->BeginLerp("player item grid left");
        else cam->BeginLerp("player item grid right");
        // gd: the `break` sits outside the `if`, so only the FIRST resource is
        // ever looked at.
        for (AmountResource* res : amounts->array_amounts) {
            if (res->itemName == itemName) res->amount_player -= 1;
            break;
        }
    }
    co_await get_tree()->create_timer(lerpDuration - .4);
    if (whichHandToGrabWith == "right") hand_defaultR->set_visible(false);
    else hand_defaultL->set_visible(false);
    dealerAI->Speaker_HandCrack();
    if (itemName == "handsaw") {
        hand_handsaw->set_visible(true);
    } else if (itemName == "magnifying glass") {
        hand_magnifier->set_visible(true);
    } else if (itemName == "handcuffs") {
        hand_handcuffs->set_visible(true);
    } else if (itemName == "cigarettes") {
        hand_cigarettepack->set_visible(true);
        itemManager->numberOfCigs_dealer -= 1;
    } else if (itemName == "beer") {
        hand_beer->set_visible(true);
    } else if (itemName == "expired medicine") {
        hand_medicine->set_visible(true);
    } else if (itemName == "inverter") {
        hand_inverter->set_visible(true);
    } else if (itemName == "burner phone") {
        hand_burnerphone->set_visible(true);
    } else if (itemName == "adrenaline") {
        hand_adrenaline->set_visible(true);
    }
    itemManager->itemArray_instances_dealer.erase(itemManager->itemArray_instances_dealer.begin() +
                                                  static_cast<std::ptrdiff_t>(matchIndex));
    godot::Node3D* gridname = indicator->dealerGridName;
    if (!stealing) itemManager->gridParentArray_enemy_available.push_back(gridname);
    if (stealing) inter->RemovePlayerItemFromGrid(activeInstance);
    activeInstance->queue_free();
    co_await get_tree()->create_timer(.2);
    ReturnHand();
    if (stealing) cam->BeginLerp("enemy");
    if (whichGridSide == "right") animator_dealerHeadLook->play("dealer look forward from right");
    else animator_dealerHeadLook->play("dealer look forward from left");
    co_await get_tree()->create_timer(lerpDuration + .01);
    HandFailsafe();
    const std::string animationName = "dealer use " + itemName;
    PlaySound(itemName);
    animator_hands->play("RESET");
    animator_hands->play(animationName);
    const double length = animator_hands->get_animation(animationName)->length;
    moving = false;
    co_await get_tree()->create_timer(length);
    stealing = false;
}

// gd: L136-L149
void HandManager::PlaySound(const std::string& itemName) {
    const std::string* stream = nullptr;
    if (itemName == "adrenaline") stream = &sound_adrenaline;
    else if (itemName == "expired medicine") stream = &sound_medicine;
    else if (itemName == "burner phone") stream = &sound_burnerphone;
    else if (itemName == "inverter") stream = &sound_inverter;
    if (!stream) return;
    speaker_interaction->set_stream(*stream);
    speaker_interaction->play();
}

// gd: L151-L164
void HandManager::RemoveItem_Remote(godot::Node3D* activeInstance) {
    auto* branch = godot::script_cast<InteractionBranch>(activeInstance->get_child(1));
    auto* tempindicator = godot::script_cast<PickupIndicator>(activeInstance->get_child(0));
    std::string lower = branch->itemName;   // gd: itemName.to_lower()
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    EraseFirst(itemManager->itemArray_dealer, lower);
    itemManager->numberOfItemsGrabbed_enemy -= 1;
    // gd: activeIndex and whichGridSide are read and never used.
    EraseFirst(itemManager->itemArray_instances_dealer, activeInstance);
    itemManager->gridParentArray_enemy_available.push_back(tempindicator->dealerGridName);
}

// gd: L166-L174. `selectedHand == "L" or "BOTH"` is always true ("BOTH" is
// truthy), so both arrays are toggled whatever the hand.
void HandManager::ToggleHandVisible(const std::string& /*selectedHand*/, bool state) {
    for (godot::Node3D* h : handArray_L) h->set_visible(state);
    for (godot::Node3D* h : handArray_R) h->set_visible(state);
}

// gd: L176-L191
void HandManager::BeginHandLerp(const std::string& whichHand, int gridIndex, const std::string& whichSide) {
    activeHandParent = whichHand == "right" ? handParent_R : handParent_L;
    pos_current = activeHandParent->get_position();   // gd: transform.origin
    rot_current = activeHandParent->get_rotation_degrees();
    orig_pos = pos_current;
    orig_rot = rot_current;
    pos_next = gridOffsetArray[static_cast<std::size_t>(gridIndex)];
    lerping_L = false;
    lerping_R = false;
    rot_next = whichSide == "right" ? handRot_right : handRot_left;
    if (whichHand == "right") lerping_R = true;
    else lerping_L = true;
    elapsed = 0;
    moving = true;
}

// gd: L193-L199
void HandManager::ReturnHand() {
    pos_current = activeHandParent->get_position();
    rot_current = activeHandParent->get_rotation_degrees();
    pos_next = orig_pos;
    rot_next = orig_rot;
    elapsed = 0;
    moving = true;
}

// gd: L201-L205
void HandManager::HandFailsafe() {
    handParent_L->set_transform_origin(parentOriginal_posL);
    handParent_L->set_rotation_degrees(parentOriginal_rotL);
    handParent_R->set_transform_origin(parentOriginal_posR);
    handParent_R->set_rotation_degrees(parentOriginal_rotR);
}

// gd: L207-L222
void HandManager::LerpHandMovement() {
    if (!moving) return;
    elapsed += get_process_delta_time();
    double c = godot::clampf(elapsed / lerpDuration, 0.0, 1.0);
    c = godot::ease(c, 0.2);
    const godot::Vector3 pos = godot::lerp(pos_current, pos_next, c);
    const godot::Vector3 rot = godot::lerp(rot_current, rot_next, c);
    if (lerping_L) {
        handParent_L->set_transform_origin(pos);
        handParent_L->set_rotation_degrees(rot);
    }
    if (lerping_R) {
        handParent_R->set_transform_origin(pos);
        handParent_R->set_rotation_degrees(rot);
    }
}

}  // namespace scripts
