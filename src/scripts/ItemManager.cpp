#include "scripts/ItemManager.h"
#include "scripts/ItemGridIndicator.h"
#include "scripts/ItemInteraction.h"

#include "godot/physics.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/CameraManager.h"
#include "scripts/CompartmentManager.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/DialogueManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/PickupIndicator.h"
#include "scripts/RoundManager.h"
#include "scripts/SpeakerController.h"
#include "scripts/gridBranch.h"
#include "scripts/ItemAmounts.h"
#include "godot/math.h"
#include "godot/rng.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace scripts {

namespace {

// gd: if ("handsaw" in array): array.erase("handsaw"). Erases the first one only.
void EraseHandsaw(std::vector<std::string>& array) {
    const auto it = std::find(array.begin(), array.end(), "handsaw");
    if (it != array.end()) array.erase(it);
}

}  // namespace

namespace {

// Vector3.lerp (vector3.h): a + (b - a) * t.
godot::Vector3 Lerp(const godot::Vector3& a, const godot::Vector3& b, double t) {
    const float f = static_cast<float>(t);
    return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
}

// A collider under an item: StaticBody3D -> CollisionShape3D, as the .gd's
// `childArray[i].get_child(0).disabled` walks it.
void SetItemCollider(godot::Node* item, bool disabled) {
    for (int i = 0; i < item->get_child_count(); ++i) {
        godot::Node* child = item->get_child(i);
        if (godot::cast_to<godot::StaticBody3D>(child)) {
            if (auto* shape = godot::cast_to<godot::CollisionShape3D>(child->get_child(0))) {
                shape->disabled = disabled;
            }
        }
    }
}

}  // namespace

bool ItemManager::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "ClearAllItems") { ClearAllItems(); return true; }
    return false;
}

// gd: L66-L70
void ItemManager::_ready() {
    playerItemStringArray.clear();
    // gd: #availableItemsToGrabArray_player = availableItemArray
    // gd: #availableItemsToGrabArray_dealer = availableItemArray
    ResetDealerGrid();
}

// gd: L72-L75
void ItemManager::_process(double /*delta*/) {
    LerpItem();
    CheckTimer();
    ItemStealTimeout();
}

// gd: L80-L88
void ItemManager::SetIntakeFocus(bool state) {
    btnParent_intake->set_visible(true);
    if (state) {
        for (godot::Control* grid : btn_grids) grid->set_visible(false);
        btn_intake->set_visible(true);
        if (cursor->controller_active) btn_intake->grab_focus();
        controller->previousFocus = btn_intake;
    } else {
        for (godot::Control* grid : btn_grids) grid->set_visible(true);
        // gd: #btn_intake.visible = false
    }
}

// gd: L91-L94
void ItemManager::ClearIntakeFocus() {
    btnParent_intake->set_visible(false);
    for (godot::Control* grid : btn_grids) grid->set_visible(false);
    btn_intake->set_visible(false);
}

// gd: L96-L114
seq::Task ItemManager::ItemClear_Remote() {
    for (int i = 0; i < itemSpawnParent->get_child_count(); ++i) {
        if (godot::script_cast<PickupIndicator>(itemSpawnParent->get_child(i)->get_child(0))) {
            requestingItemClear = true;
            break;
        }
    }
    if (requestingItemClear) {
        camera->BeginLerp("home");
        co_await get_tree()->create_timer(.8);
        SetupItemClear();
        comp->animator_compartment->play("clear items");
        co_await get_tree()->create_timer(1.6);
        comp->animator_compartment->play("hide items");
        co_await get_tree()->create_timer(.8);
    } else {
        camera->BeginLerp("home");
        co_await get_tree()->create_timer(.8);
        comp->animator_compartment->play("hide items");
        co_await get_tree()->create_timer(.8);
    }
}

// gd: L118-L174
seq::Task ItemManager::BeginItemGrabbing() {
    roundManager->ignoring = false;
    numberOfItemsGrabbed = 0;
    if (roundManager->playerData->hasReadItemSwapIntroduction) camera->BeginLerp("home");
    else camera->BeginLerp("enemy");
    // gd: #CLEAR ITEMS FROM TABLE
    if (newBatchHasBegun) {
        for (int i = 0; i < itemSpawnParent->get_child_count(); ++i) {
            if (godot::script_cast<PickupIndicator>(itemSpawnParent->get_child(i)->get_child(0))) {
                requestingItemClear = true;
                break;
            }
        }
        if (requestingItemClear) {
            co_await get_tree()->create_timer(.8);
            SetupItemClear();
            comp->animator_compartment->play("clear items");
            co_await get_tree()->create_timer(1.4);
        }
        newBatchHasBegun = false;
    }

    co_await get_tree()->create_timer(.8);
    if (!roundManager->playerData->hasReadItemSwapIntroduction) {
        get_tree()->spawn(dialogue->ShowText_ForDuration(godot::tr("MORE INTERESTING"), 3));
        co_await get_tree()->create_timer(3);
        camera->BeginLerp("home");
        co_await get_tree()->create_timer(.8);
        roundManager->playerData->hasReadItemSwapIntroduction = true;
    }
    comp->CycleCompartment("show briefcase");
    co_await get_tree()->create_timer(.8);
    if (comp->isHiding_items) comp->CycleCompartment("show items");
    co_await get_tree()->create_timer(.8);
    camera->BeginLerp("briefcase");
    co_await get_tree()->create_timer(.8);

    const RoundClass& round =
        *roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)];
    if (!roundManager->playerData->hasReadItemDistributionIntro) {
        const int stringIndex = round.numberOfItemsToGrab;
        std::string string = stringNumberArray[static_cast<std::size_t>(stringIndex)];
        // gd: the lookup above is dead: the next line overwrites it. Kept
        // because the source has it.
        string = std::to_string(stringIndex);
        dialogue->ShowText_Forever(godot::format(godot::tr("ITEMS EACH"), {string}));
        co_await get_tree()->create_timer(2.5);
        dialogue->ShowText_Forever(godot::tr("MORE ITEMS"));
        co_await get_tree()->create_timer(2.5);
        dialogue->HideText();
        roundManager->playerData->hasReadItemDistributionIntro = true;
    }

    if (!roundManager->playerData->hasReadItemDistributionIntro2 && round.hasIntro2) {
        const int stringIndex = round.numberOfItemsToGrab;
        std::string string = stringNumberArray[static_cast<std::size_t>(stringIndex)];
        string = std::to_string(stringIndex);   // gd: dead lookup, as above
        dialogue->ShowText_Forever(godot::format(godot::tr("ITEMS EACH"), {string}));
        co_await get_tree()->create_timer(2.5);
        dialogue->HideText();
        roundManager->playerData->hasReadItemDistributionIntro2 = true;
    }
    // gd: #ALLOW ITEM GRAB
    cursor->SetCursor(true, true);
    SetIntakeFocus(true);
    interaction_intake->interactionAllowed = true;
}

// gd: L176-L189
seq::Task ItemManager::EndItemGrabbing() {
    GrabItems_Enemy();
    GridParents(false);
    interaction_intake->interactionAllowed = false;
    cursor->SetCursor(false, false);
    ClearIntakeFocus();
    co_await get_tree()->create_timer(.45);
    comp->CycleCompartment("hide briefcase");
    co_await get_tree()->create_timer(1);
    camera->BeginLerp("home");
    co_await get_tree()->create_timer(.9);
    moving = false;
    roundManager->ReturnFromItemGrabbing();
}

// gd: L191-L208
seq::Task ItemManager::GrabSpook() {
    interaction_intake->interactionAllowed = false;
    cursor->SetCursor(false, false);
    ClearIntakeFocus();
    PlayItemGrabSound();
    speakercontroller_musicmain->SnapVolume(false);
    speaker_god->play();
    anim_spook->play("grab form");
    co_await get_tree()->create_timer(2 + .8);
    anim_spook->play("put back");
    PlayItemGrabSound();
    camera->BeginLerp("briefcase");
    speaker_god->stop();
    speakercontroller_musicmain->FadeIn();
    co_await get_tree()->create_timer(.8);
    cursor->SetCursor(true, true);
    SetIntakeFocus(true);
    interaction_intake->interactionAllowed = true;
}

// gd: L213-L308
seq::Task ItemManager::GrabItem() {
    PlayerData& playerData = *roundManager->playerData;
    if (playerData.currentBatchIndex == 1 && roundManager->currentRound == 1) {
        if (spook_counter == 1 && !spook_fired && !playerData.seenGod) {
            get_tree()->spawn(GrabSpook());
            playerData.seenGod = true;
            spook_fired = true;
            co_return;
        }
        ++spook_counter;
    }
    // gd: #GET RANDOM ITEM
    PlayItemGrabSound();
    interaction_intake->interactionAllowed = false;
    ItemResource* selectedResource = nullptr;

    // gd: #SET PLAYER AVAILABLE ITEMS ACCORDING TO MAX COUNTS
    // amount_main is a CAP, not a weight: the pick below is UNIFORM.
    std::vector<AmountResource*>& amountArray = amounts->array_amounts;
    availableItemsToGrabArray_player.clear();
    for (AmountResource* res : amountArray) {
        if (res->amount_active == 0) continue;
        if (res->amount_player != res->amount_active) {
            availableItemsToGrabArray_player.push_back(res->itemName);
        }
    }
    // gd: #for res in amountArray: availableItemsToGrabArray_player.append(res.itemName)

    if (roundManager->currentRound == 0 &&
        roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)]->startingHealth == 2) {
        EraseHandsaw(availableItemsToGrabArray_player);
    }
    randindex = static_cast<int>(godot::randi_range(
        0, static_cast<int64_t>(availableItemsToGrabArray_player.size()) - 1));

    ++numberOfItemsGrabbed;
    // gd: #SPAWN ITEM. This loop has no break, so the last match wins.
    for (ItemResource* r : instanceArray) {
        if (availableItemsToGrabArray_player[static_cast<std::size_t>(randindex)] == r->itemName) {
            selectedResource = r;
        }
    }
    // The node is handed to the tree below, which owns it from then on.
    std::unique_ptr<godot::Node> spawned = selectedResource->instance->instantiate();
    auto* itemInstance = godot::cast_to<godot::Node3D>(spawned.get());

    for (AmountResource* res : amountArray) {
        if (selectedResource->itemName == res->itemName) {
            ++res->amount_player;
            break;
        }
    }

    activeItem = itemInstance;
    // PORT: add_child takes ownership, as ShellSpawner's does; activeItem keeps
    // the raw pointer the .gd holds.
    itemSpawnParent->add_child(std::move(spawned));
    itemInstance->set_transform_origin(selectedResource->pos_inBriefcase);
    itemInstance->set_rotation_degrees(selectedResource->rot_inBriefcase);
    activeItem_offset_pos = selectedResource->pos_offset;
    activeItem_offset_rot = selectedResource->rot_offset;
    // gd: #ADD ITEM TO PICKUP INDICATOR & INTERACTION BRANCH ARRAY
    if (numberOfOccupiedGrids != 8) {
        temp_indicator = godot::script_cast<PickupIndicator>(activeItem->get_child(0));
        temp_interaction = godot::script_cast<InteractionBranch>(activeItem->get_child(1));
        items_dynamicIndicatorArray.push_back(temp_indicator);
        items_dynamicInteractionArray.push_back(temp_interaction);
        const int ind = static_cast<int>(items_dynamicIndicatorArray.size()) - 1;
        temp_indicator->locationInDynamicArray = ind;
    }
    // gd: #DISABLE ITEM COLLIDER
    SetItemCollider(activeItem, true);
    // gd: #LERP TO HAND
    pos_current = itemInstance->get_position();
    rot_current = itemInstance->get_rotation_degrees();
    pos_next = selectedResource->pos_inHand;
    rot_next = selectedResource->rot_inHand;
    elapsed = 0;
    moving = true;
    co_await get_tree()->create_timer(lerpDuration - .2);
    if (!roundManager->playerData->indicatorShown) grid->ShowGridIndicator();

    if (numberOfOccupiedGrids != 8) {
        GridParents(true);
        SetIntakeFocus(false);
    } else {
        // gd: #NOT ENOUGH SPACE. PUT ITEM BACK AND END ITEM GRABBING
        for (AmountResource* res : amountArray) {
            if (selectedResource->itemName == res->itemName) {
                --res->amount_player;
                break;
            }
        }
        dialogue->ShowText_Forever(godot::tr("NO SPACE"));
        co_await get_tree()->create_timer(1.8);
        dialogue->ShowText_Forever(godot::tr("UNFORTUNATE"));
        co_await get_tree()->create_timer(2.2);
        dialogue->HideText();
        pos_current = activeItem->get_position();
        rot_current = activeItem->get_rotation_degrees();
        pos_next = selectedResource->pos_inBriefcase;
        rot_next = selectedResource->rot_inBriefcase;
        elapsed = 0;
        moving = true;
        cursor->SetCursor(false, false);
        ClearIntakeFocus();
        PlayItemGrabSound();
        co_await get_tree()->create_timer(lerpDuration);
        moving = false;
        activeItem->queue_free();
        get_tree()->spawn(EndItemGrabbing());
    }
}

// gd: L310-L341
seq::Task ItemManager::PlaceDownItem(int gridIndex) {
    ++numberOfOccupiedGrids;
    gridOccupiedArray[static_cast<std::size_t>(gridIndex)] = true;
    moving = false;
    // gd: `var temp_indicator` here shadows the member: a local, not the field.
    PickupIndicator* temp_indicator_local = godot::script_cast<PickupIndicator>(activeItem->get_child(0));
    if (temp_interaction->itemName == "cigarettes") ++numberOfCigs_player;
    pos_current = activeItem->get_position();
    rot_current = activeItem->get_rotation_degrees();
    const godot::Node3D* slot = gridParentArray[static_cast<std::size_t>(gridIndex)];
    const godot::Vector3 slotPos = slot->get_position();
    const godot::Vector3 slotRot = slot->get_rotation_degrees();
    pos_next = {slotPos.x + activeItem_offset_pos.x, slotPos.y + activeItem_offset_pos.y,
                slotPos.z + activeItem_offset_pos.z};
    rot_next = {slotRot.x + activeItem_offset_rot.x, slotRot.y + activeItem_offset_rot.y,
                slotRot.z + activeItem_offset_rot.z};
    temp_interaction->isPlayerSide = true;
    temp_indicator_local->dealerGridIndex = gridIndex + 8;
    elapsed = 0;
    moving = true;
    temp_interaction->itemGridIndex = gridIndex;
    if (!temp_indicator_local->sound_placeDown.empty()) {
        speaker_placedown->set_stream(temp_indicator_local->sound_placeDown);
        speaker_placedown->play();
    }
    SetItemCollider(activeItem, false);
    const RoundClass& round =
        *roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)];
    if (numberOfItemsGrabbed == round.numberOfItemsToGrab) {
        // gd: #END ITEM GRABBING
        get_tree()->spawn(EndItemGrabbing());
    } else {
        // gd: #GRAB NEXT ITEM
        GridParents(false);
        ClearIntakeFocus();
        co_await get_tree()->create_timer(lerpDuration);
        interaction_intake->interactionAllowed = true;
        SetIntakeFocus(true);
    }
}

// gd: L344-L394
void ItemManager::GrabItems_Enemy() {
    ItemResource* selectedResource = nullptr;
    const RoundClass& round =
        *roundManager->roundArray[static_cast<std::size_t>(roundManager->currentRound)];
    for (int i = 0; i < round.numberOfItemsToGrab; ++i) {
        if (numberOfItemsGrabbed_enemy == 8) continue;
        std::vector<AmountResource*>& amountArray = amounts->array_amounts;
        availableItemsToGrabArray_dealer.clear();
        for (AmountResource* res : amountArray) {
            if (res->amount_active == 0) continue;
            if (res->amount_dealer != res->amount_active) {
                availableItemsToGrabArray_dealer.push_back(res->itemName);
            }
        }
        if (roundManager->currentRound == 0 && round.startingHealth == 2) {
            EraseHandsaw(availableItemsToGrabArray_dealer);
        }
        // Draw 1 of 2: the item.
        const int pick = static_cast<int>(godot::randi_range(
            0, static_cast<int64_t>(availableItemsToGrabArray_dealer.size()) - 1));
        const std::string selectedItem = availableItemsToGrabArray_dealer[static_cast<std::size_t>(pick)];

        // gd: #SPAWN ITEM
        for (ItemResource* r : instanceArray_dealer) {
            if (selectedItem == r->itemName) {
                selectedResource = r;
                // gd: #ADD STRING TO DEALER ITEM ARRAY
                std::string lower = r->itemName;
                for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                itemArray_dealer.push_back(lower);
                break;
            }
        }
        std::unique_ptr<godot::Node> spawned = selectedResource->instance->instantiate();
        auto* itemInstance = godot::cast_to<godot::Node3D>(spawned.get());
        PickupIndicator* temp_itemIndicator = godot::script_cast<PickupIndicator>(itemInstance->get_child(0));
        temp_itemIndicator->isDealerItem = true;
        for (AmountResource* res : amountArray) {
            if (selectedResource->itemName == res->itemName) {
                ++res->amount_dealer;
                break;
            }
        }

        // gd: #ADD INSTANCE TO DEALER ITEM ARRAY (mida vittu this code is getting out of hand)
        itemArray_instances_dealer.push_back(itemInstance);
        activeItem_enemy = itemInstance;
        itemSpawnParent->add_child(std::move(spawned));

        // gd: #PLACE ITEM ON RANDOM GRID. Draw 2 of 2: the slot.
        const int randgrid = static_cast<int>(godot::randi_range(
            0, static_cast<int64_t>(gridParentArray_enemy_available.size()) - 1));
        godot::Node3D* gridname = gridParentArray_enemy_available[static_cast<std::size_t>(randgrid)];
        const godot::Vector3 gridPos = gridname->get_position();
        const godot::Vector3 gridRot = gridname->get_rotation_degrees();
        activeItem_enemy->set_transform_origin({gridPos.x + selectedResource->pos_offset.x,
                                        gridPos.y + selectedResource->pos_offset.y,
                                        gridPos.z + selectedResource->pos_offset.z});
        activeItem_enemy->set_rotation_degrees({gridRot.x + selectedResource->rot_offset.x,
                                                gridRot.y + selectedResource->rot_offset.y,
                                                gridRot.z + selectedResource->rot_offset.z});
        if (activeItem_enemy->get_position().z > 0) temp_itemIndicator->whichSide = "right";
        else temp_itemIndicator->whichSide = "left";
        temp_itemIndicator->dealerGridIndex =
            godot::script_cast<gridBranch>(gridname->get_child(0))->activeIndex;
        temp_itemIndicator->dealerGridName = gridname;
        if (godot::script_cast<InteractionBranch>(activeItem_enemy->get_child(1))->itemName == "cigarettes") {
            ++numberOfCigs_dealer;
        }
        gridParentArray_enemy_available.erase(std::find(gridParentArray_enemy_available.begin(),
                                                        gridParentArray_enemy_available.end(), gridname));
        ++numberOfItemsGrabbed_enemy;
    }
}

// gd: L568-L576
void ItemManager::LerpItem() {
    if (moving) {
        elapsed += get_tree()->get_process_delta_time();
        double c = std::clamp(elapsed / lerpDuration, 0.0, 1.0);
        c = godot::ease(c, 0.4);
        const godot::Vector3 pos = Lerp(pos_current, pos_next, c);
        const godot::Vector3 rot = Lerp(rot_current, rot_next, c);
        activeItem->set_transform_origin(pos);
        activeItem->set_rotation_degrees(rot);
    }
}

// gd: L473-L474
void ItemManager::ItemStealTimeout() {
    if (counting) timer_steal_current += get_tree()->get_process_delta_time();
}

// gd: L477-L481
void ItemManager::CheckTimer() {
    if ((timer_steal_current > timer_steal_max) && checking && !fs) {
        // gd: RevertItemSteal_Timeout(), not awaited: spawned.
        get_tree()->spawn(RevertItemSteal_Timeout());
        fs = true;
    }
}

// gd: L399-L432
seq::Task ItemManager::SetupItemSteal() {
    // gd: PREVIOUS = items; items = []. A reference swap; a copy and a clear
    // leave the same two lists.
    PREVIOUS_items_dyanmicIndicatorArray = items_dynamicIndicatorArray;
    PREVIOUS_items_dynamicInteractionArray = items_dynamicInteractionArray;
    items_dynamicIndicatorArray.clear();
    items_dynamicInteractionArray.clear();
    interaction->pos_hand = interaction->pos_hand_stealing;
    interaction->rot_hand = interaction->rot_hand_stealing;

    interaction->stealing = true;

    for (int c = 0; c < itemSpawnParent->get_child_count(); ++c) {
        godot::Node* child = itemSpawnParent->get_child(c);
        if (auto* temp_indicator = godot::script_cast<PickupIndicator>(child->get_child(0))) {
            items_dynamicIndicatorArray.push_back(temp_indicator);
            items_dynamicInteractionArray.push_back(godot::script_cast<InteractionBranch>(child->get_child(1)));
        }
    }

    camera->BeginLerp("enemy items");
    co_await get_tree()->create_timer(.7);
    interaction->stealing_fs = true;
    interaction->EnablePermissions();
    interaction->DisableShotgun();
    roundManager->ClearDeskUI(true);
    btnParent_stealing->set_visible(true);
    if (cursor->controller_active) btnparent_ff_stealing->grab_focus();
    controller->previousFocus = btnparent_ff_stealing;
    // gd: a second loop over the children that only declares two locals.
    Counter(true);
}

// gd: L434-L437
void ItemManager::RevertItemSteal() {
    btnParent_stealing->set_visible(false);
    items_dynamicIndicatorArray = PREVIOUS_items_dyanmicIndicatorArray;
    items_dynamicInteractionArray = PREVIOUS_items_dynamicInteractionArray;
}

// gd: L439-L455
seq::Task ItemManager::RevertItemSteal_Timeout() {
    btnParent_stealing->set_visible(false);
    interaction->stealing = false;
    interaction->DisablePermissions();
    items_dynamicIndicatorArray = PREVIOUS_items_dyanmicIndicatorArray;
    items_dynamicInteractionArray = PREVIOUS_items_dynamicInteractionArray;
    for (int c = 0; c < itemSpawnParent->get_child_count(); ++c) {
        if (auto* temp_indicator = godot::script_cast<PickupIndicator>(itemSpawnParent->get_child(c)->get_child(0))) {
            temp_indicator->Revert();
        }
    }
    camera->BeginLerp("home");
    co_await get_tree()->create_timer(.4);
    interaction->stealing_fs = false;
    interaction->pos_hand = interaction->pos_hand_main;
    interaction->rot_hand = interaction->rot_hand_main;
    interaction->EnablePermissions();
}

// gd: L457-L467
void ItemManager::Counter(bool starting) {
    if (starting) {
        timer_steal_current = 0;
        counting = true;
        checking = true;
        fs = false;
    } else {
        timer_steal_current = 0;
        counting = false;
        checking = false;
        fs = true;
    }
}

// gd: L483-L488
void ItemManager::ResetDealerGrid() {
    // gd: #CLEAR DEALER GRID
    numberOfItemsGrabbed_enemy = 0;
    gridParentArray_enemy_available.clear();
    for (godot::Node3D* g : gridParentArray_enemy) gridParentArray_enemy_available.push_back(g);
}

// gd: L490-L538
void ItemManager::SetupItemClear() {
    instancesToDelete.clear();
    // gd: #DEALER
    numberOfCigs_dealer = 0;
    numberOfItemsGrabbed_enemy = 0;
    gridParentArray_enemy_available.clear();
    for (godot::Node3D* g : gridParentArray_enemy) gridParentArray_enemy_available.push_back(g);
    itemArray_dealer.clear();

    // gd: #PLAYER
    numberOfCigs_player = 0;
    items_dynamicIndicatorArray.clear();
    items_dynamicInteractionArray.clear();
    for (std::size_t i = 0; i < gridOccupiedArray.size(); ++i) gridOccupiedArray[i] = false;
    numberOfOccupiedGrids = 0;

    for (int i = 0; i < itemSpawnParent->get_child_count(); ++i) {
        godot::Node* child = itemSpawnParent->get_child(i);
        if (godot::script_cast<PickupIndicator>(child->get_child(0))) {
            instancesToDelete.push_back(godot::cast_to<godot::Node3D>(child));
        }
    }

    // gd: #PARENT INSTANCES TO APPROPRIATE GRID COMPARTMENT PARENT
    // The children are copied first: the loop re-parents what it walks.
    std::vector<godot::Node*> spawnChildren;
    for (int i = 0; i < itemSpawnParent->get_child_count(); ++i) spawnChildren.push_back(itemSpawnParent->get_child(i));
    for (godot::Node* node : spawnChildren) {
        if (!godot::script_cast<PickupIndicator>(node->get_child(0))) continue;
        auto* item = godot::cast_to<godot::Node3D>(node);
        const float objPos_x = item->get_position().x;
        const float objPos_z = item->get_position().z;
        // gd: four separate ifs, not an if/else chain: an item at exactly
        // x == 0 or z == 0 matches none and stays where it is.
        godot::Node3D* target = nullptr;
        if (objPos_x < 0 && objPos_z > 0) target = gridParent_1;
        if (objPos_x < 0 && objPos_z < 0) target = gridParent_2;
        if (objPos_x > 0 && objPos_z > 0) target = gridParent_3;
        if (objPos_x > 0 && objPos_z < 0) target = gridParent_4;
        if (!target) continue;
        const core::Mat4 orig = item->get_global_transform();
        item->get_parent()->remove_child(item);
        target->add_child(static_cast<godot::Node*>(item));
        item->set_global_transform(orig);
    }
}

// gd: L541-L550
void ItemManager::ClearAllItems() {
    // gd: #DELETE INSTANCES
    for (godot::Node3D* n : instancesToDelete) n->queue_free();
    itemArray_instances_dealer.clear();
    for (AmountResource* res : amounts->array_amounts) {
        res->amount_dealer = 0;
        res->amount_player = 0;
    }
}

// gd: L552-L560
void ItemManager::GridParents(bool setState) {
    for (std::size_t i = 0; i < gridParentArray.size(); ++i) {
        auto* shape = static_cast<godot::CollisionShape3D*>(
            gridParentArray[i]->get_child(1)->get_child(0));
        auto* branch = godot::script_cast<InteractionBranch>(gridParentArray[i]->get_child(0));
        if (setState) {
            shape->disabled = false;
            if (!gridOccupiedArray[i]) branch->interactionAllowed = true;
        } else {
            shape->disabled = true;
            branch->interactionAllowed = false;
        }
    }
}

// gd: L562-L566
void ItemManager::PlayItemGrabSound() {
    speaker_itemgrab->set_stream(soundArray_itemGrab[static_cast<std::size_t>(itemGrabSoundIndex)]);
    speaker_itemgrab->play();
    if (itemGrabSoundIndex != static_cast<int>(soundArray_itemGrab.size()) - 1) ++itemGrabSoundIndex;
    else itemGrabSoundIndex = 0;
}

}  // namespace scripts
