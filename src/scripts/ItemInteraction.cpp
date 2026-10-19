#include "scripts/ItemInteraction.h"

#include "scripts/AchievementManager.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/CursorManager.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/CameraManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/ItemAmounts.h"
#include "scripts/ItemManager.h"
#include "scripts/HandManager.h"
#include "scripts/FilterController.h"
#include "scripts/MedicineManager.h"
#include "scripts/PermissionManager.h"
#include "scripts/PickupIndicator.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/resources.h"

#include <algorithm>
#include <iterator>

namespace scripts {

namespace {

// Vector3.lerp (vector3.h): a + (b - a) * t.
godot::Vector3 Lerp(const godot::Vector3& a, const godot::Vector3& b, double t) {
    const float f = static_cast<float>(t);
    return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
}

}  // namespace

bool ItemInteraction::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "AdrenalineHit") {
        get_tree()->spawn(AdrenalineHit());   // a method track does not await
        return true;
    }
    if (method == "PlaySound_BreakHandcuffs") {
        PlaySound_BreakHandcuffs();
        return true;
    }
    return false;
}

// gd: L42-L44
void ItemInteraction::_ready() {
    pos_hand_main = pos_hand;
    rot_hand_main = rot_hand;
}

// gd: L46-L47
void ItemInteraction::_process(double /*delta*/) {
    LerpMovement();
}

// gd: L51-L103
seq::Task ItemInteraction::PickupItemFromTable(godot::Node3D* itemParent, std::string passedItemName) {
    if (stealing) items->Counter(false);
    // gd: #SET INTERACTION PERMISSIONS, HIDE CURSOR
    perm->SetIndicators(false);
    perm->SetInteractionPermissions(false);
    perm->RevertDescriptionUI();
    cursor->SetCursor(false, false);
    roundManager->ClearDeskUI(true);
    // gd: #GET VARIABLES
    temp_itemParent = itemParent;
    temp_indicator = godot::script_cast<PickupIndicator>(itemParent->get_child(0));
    temp_interaction = godot::script_cast<InteractionBranch>(itemParent->get_child(1));
    // gd: #STEAL
    if (stealing && stealing_fs) {
        hands->RemoveItem_Remote(temp_itemParent);
        items->RevertItemSteal();
    }
    // gd: #PLAY PICKUP SOUND
    speaker_pickup->set_stream(temp_indicator->sound_pickup);
    speaker_pickup->set_pitch_scale(static_cast<float>(godot::randf_range(.93, 1.0)));
    speaker_pickup->play();
    temp_indicator->lerpEnabled = false;
    temp_indicator->interactionAllowed = false;
    temp_indicator->SnapToMax();
    temp_interaction->interactionAllowed = false;
    const std::string temp_name = temp_interaction->itemName;
    // gd: #LERP
    pos_current = temp_itemParent->get_position();
    rot_current = temp_itemParent->get_rotation_degrees();
    pos_next = pos_hand;
    rot_next = rot_hand;
    elapsed = 0;
    moving = true;
    co_await get_tree()->create_timer(lerpDuration - .1);
    moving = false;
    if (!stealing) RemovePlayerItemFromGrid(temp_itemParent);
    temp_itemParent->queue_free();
    if (stealing) {
        camera->BeginLerp("home");
        stealing_fs = false;
        co_await get_tree()->create_timer(.4);
        pos_hand = pos_hand_main;
        rot_hand = rot_hand_main;
        for (AmountResource* res : amounts->array_amounts) {
            if (res->itemName == temp_name) {
                res->amount_dealer -= 1;
                break;
            }
        }
    }
    // gd: InteractWith(passedItemName), not awaited: spawned.
    get_tree()->spawn(InteractWith(passedItemName));
    stealing = false;
}

// gd: L105-L184
seq::Task ItemInteraction::InteractWith(std::string itemName) {
    // gd: #INTERACTION
    for (AmountResource* res : amounts->array_amounts) {
        if (res->itemName == itemName) {
            res->amount_player -= 1;
            break;
        }
    }
    std::vector<std::string>& turnItems = roundManager->playerCurrentTurnItemArray;
    if (std::find(turnItems.begin(), turnItems.end(), itemName) == turnItems.end()) turnItems.push_back(itemName);

    if (itemName == "handcuffs") {
        // gd: L118-L129
        animator_dealerHands->play("dealer get handcuffed");
        co_await get_tree()->create_timer(1);
        camera->BeginLerp("enemy");
        co_await get_tree()->create_timer(1.3);
        camera->BeginLerp("dealer handcuffs");
        roundManager->dealerCuffed = true;
        dealerIntelligence->dealerAboutToBreakFree = false;
        co_await get_tree()->create_timer(1.3);
        camera->BeginLerp("home");
        co_await get_tree()->create_timer(.6);
        EnablePermissions();
    } else if (itemName == "beer") {
        // gd: L130-L141
        roundManager->playerData->stat_beerDrank += 330;
        bool isFinalShell = false;
        if (roundManager->shellSpawner->sequenceArray.size() == 1) isFinalShell = true;
        animator_playerHands->play("player use beer");
        co_await get_tree()->create_timer(1.4);
        shellEject_player->FadeOutShell();
        co_await get_tree()->create_timer(4.2);
        // gd: #check if ejected last shell
        if (!isFinalShell) {
            EnablePermissions();
        } else {
            // gd: roundManager.StartRound(true), not awaited: spawned.
            get_tree()->spawn(roundManager->StartRound(true));
        }
    } else if (itemName == "magnifying glass") {
        // gd: L142-L146
        animator_playerHands->play("player use magnifier");
        const assets::Animation* anim = animator_playerHands->get_animation("player use magnifier");
        const double length = anim ? anim->length : 0.0;
        co_await get_tree()->create_timer(length + .2);
        EnablePermissions();
    } else if (itemName == "cigarettes") {
        // gd: L147-L152
        roundManager->playerData->stat_cigSmoked += 1;
        animator_playerHands->play("player use cigarettes");
        co_await get_tree()->create_timer(5);
        itemManager->numberOfCigs_player -= 1;
        EnablePermissions();
    } else if (itemName == "handsaw") {
        // gd: L153-L158
        animator_playerHands->play("player use handsaw");
        roundManager->barrelSawedOff = true;
        roundManager->currentShotgunDamage = 2;
        co_await get_tree()->create_timer(4.28 + .2);
        EnablePermissions();
    } else if (itemName == "expired medicine") {
        // gd: L159-L164. No EnablePermissions here: Medicine.Perms does it.
        PlaySound(sound_use_medicine);
        animator_playerHands->play("player use expired pills");
        // gd: medicine.UseMedicine(), not awaited: spawned.
        get_tree()->spawn(medicine->UseMedicine());
    } else if (itemName == "inverter") {
        // gd: L165-L171
        PlaySound(sound_use_inverter);
        animator_playerHands->play("player use inverter");
        std::string& chamber = roundManager->shellSpawner->sequenceArray[0];
        if (chamber == "live") {
            chamber = "blank";
        } else {
            chamber = "live";
        }
        co_await get_tree()->create_timer(3.2);
        EnablePermissions();
    } else if (itemName == "burner phone") {
        // gd: L172-L176
        PlaySound(sound_use_burnerphone);
        animator_playerHands->play("player use burner phone");
        co_await get_tree()->create_timer(7.9);
        EnablePermissions();
    } else if (itemName == "adrenaline") {
        // gd: L177-L182. No EnablePermissions: the steal does it.
        PlaySound(sound_use_adrenaline);
        animator_playerHands->play("player use adrenaline");
        co_await get_tree()->create_timer(5.3 + .2);
        // gd: items.SetupItemSteal(), not awaited: spawned.
        get_tree()->spawn(items->SetupItemSteal());
    }
    CheckAchievement_koni();
    CheckAchievement_full();
}

// gd: L197-L201
seq::Task ItemInteraction::AdrenalineHit() {
    anim_pp->play("adrenaline brightness");
    filter->BeginPan(filter->lowPassDefaultValue, filter->lowPassMaxValue);
    co_await get_tree()->create_timer(6);
    // gd: filter.effect_lowPass.cutoff_hz; FilterController holds the bus
    // effect's cutoff as effect_lowPass_cutoff_hz.
    filter->BeginPan(filter->effect_lowPass_cutoff_hz, filter->lowPassDefaultValue);
}

// gd: L203-L205
void ItemInteraction::PlaySound(const std::string& clip) {
    speaker_interaction->set_stream(clip);
    speaker_interaction->play();
}

// gd: L207-L213
void ItemInteraction::EnablePermissions() {
    perm->SetStackInvalidIndicators();
    perm->SetIndicators(true);
    perm->SetInteractionPermissions(true);
    perm->RevertDescriptionUI();
    cursor->SetCursor(true, true);
    roundManager->SetupDeskUI();
}

// gd: L215-L216
void ItemInteraction::DisableShotgun() {
    perm->DisableShotgun();
}

// gd: L218-L223
void ItemInteraction::DisablePermissions() {
    perm->SetIndicators(false);
    perm->SetInteractionPermissions(false);
    perm->RevertDescriptionUI();
    cursor->SetCursor(false, false);
    roundManager->ClearDeskUI(true);
}

// gd: L225-L226
void ItemInteraction::PlaySound_BreakHandcuffs() {
    speaker_breakcuffs->play();
}

// gd: L228-L234
void ItemInteraction::RemovePlayerItemFromGrid(godot::Node3D* parent) {
    auto* indic = godot::script_cast<PickupIndicator>(parent->get_child(0));
    auto* inter = godot::script_cast<InteractionBranch>(parent->get_child(1));
    auto& indicators = itemManager->items_dynamicIndicatorArray;
    auto& interactions = itemManager->items_dynamicInteractionArray;
    // gd: Array.erase removes the FIRST occurrence only.
    if (auto it = std::find(indicators.begin(), indicators.end(), indic); it != indicators.end()) indicators.erase(it);
    if (auto it = std::find(interactions.begin(), interactions.end(), inter); it != interactions.end()) {
        interactions.erase(it);
    }
    itemManager->gridOccupiedArray[static_cast<std::size_t>(inter->itemGridIndex)] = false;
    itemManager->numberOfOccupiedGrids -= 1;
}

// gd: L236-L242
void ItemInteraction::LerpMovement() {
    if (moving) {
        elapsed += get_tree()->get_process_delta_time();
        const double c = std::clamp(elapsed / lerpDuration, 0.0, 1.0);
        // gd: #c = ease(c, 0.1), commented out in the .gd.
        temp_itemParent->set_transform_origin(Lerp(pos_current, pos_next, c));
        temp_itemParent->set_rotation_degrees(Lerp(rot_current, rot_next, c));
    }
}

// gd: L187-L188
void ItemInteraction::CheckAchievement_koni() {
    const auto& turn = roundManager->playerCurrentTurnItemArray;
    const auto has = [&](const char* item) { return std::find(turn.begin(), turn.end(), item) != turn.end(); };
    if (has("cigarettes") && has("beer") && has("expired medicine")) ach->UnlockAchievement("ach9");
}

// gd: L190-L193. Compares sizes only, as the original does.
void ItemInteraction::CheckAchievement_full() {
    static const char* const all[] = {"handsaw", "magnifying glass", "beer", "cigarettes", "handcuffs",
                                      "expired medicine", "burner phone", "adrenaline", "inverter"};
    const auto& pl = roundManager->playerCurrentTurnItemArray;
    if (std::size(all) == pl.size()) ach->UnlockAchievement("ach12");
}

}  // namespace scripts
