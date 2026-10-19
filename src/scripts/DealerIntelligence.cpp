#include "scripts/DealerIntelligence.h"
#include "scripts/DefibCutter.h"
#include "scripts/HealthCounter.h"
#include "scripts/PickupIndicator.h"
#include "scripts/MedicineManager.h"
#include "scripts/ItemManager.h"
#include "scripts/ItemAmounts.h"
#include "scripts/InteractionBranch.h"
#include "scripts/HandManager.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/CameraManager.h"
#include "scripts/CameraShaker.h"
#include "scripts/DeathManager.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/ShellLoader.h"
#include "scripts/ShellSpawner.h"
#include "scripts/ShotgunShooting.h"
#include "scripts/SmokeController.h"

#include <algorithm>

namespace scripts {

// METHOD track keys and calls by name. `dealer hands on table` (animator_dealer
// hands) calls Speaker_DealerArrive at t = 0.
bool DealerIntelligence::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "Speaker_DealerArrive") {
        Speaker_DealerArrive();
        return true;
    }
    if (method == "Speaker_DealerArrive_Cuffed") {
        Speaker_DealerArrive_Cuffed();
        return true;
    }
    if (method == "Speaker_HandCrack") {
        Speaker_HandCrack();
        return true;
    }
    if (method == "Animator_CheckHandcuffs") {
        Animator_CheckHandcuffs();
        return true;
    }
    if (method == "Animator_GiveHandcuffs") {
        Animator_GiveHandcuffs();
        return true;
    }
    return false;
}

// gd: L396-L400
void DealerIntelligence::Speaker_DealerArrive() {
    const double p = godot::randf_range(.9, 1.0);
    speaker_dealerarrive->set_pitch_scale(static_cast<float>(p));
    speaker_dealerarrive->set_stream(sound_dealerarrive);
    speaker_dealerarrive->play();
}

// gd: L402-L406
void DealerIntelligence::Speaker_DealerArrive_Cuffed() {
    const double p = godot::randf_range(.9, 1.0);
    speaker_dealerarrive->set_pitch_scale(static_cast<float>(p));
    speaker_dealerarrive->set_stream(sound_dealerarriveCuffed);
    speaker_dealerarrive->play();
}

// gd: L408-L411
void DealerIntelligence::Speaker_HandCrack() {
    const auto randindex = godot::randi_range(0, static_cast<int64_t>(soundArray_cracks.size()) - 1);
    speaker_handCrack->set_stream(soundArray_cracks[static_cast<std::size_t>(randindex)]);
    speaker_handCrack->play();
}

// gd: L39-L57
seq::Task DealerIntelligence::DealerCheckHandCuffs() {
    bool brokeFree = false;
    camera->BeginLerp("enemy cuffs close");
    if (!dealerAboutToBreakFree) {
        co_await get_tree()->create_timer(.3);
        speaker_checkHandcuffs->play();
        animator_dealerHands->play("dealer check handcuffs");
        co_await get_tree()->create_timer(.7);
        brokeFree = false;
    } else {
        co_await get_tree()->create_timer(.3);
        animator_dealerHands->play("dealer break cuffs");   // gd: #replace with broke free anim
        speaker_breakHandcuffs->play();
        brokeFree = true;
        roundManager->dealerCuffed = false;
        dealerAboutToBreakFree = false;
    }
    dealerAboutToBreakFree = true;
    // gd: roundManager.ReturnFromCuffCheck(brokeFree), not awaited: spawned.
    get_tree()->spawn(roundManager->ReturnFromCuffCheck(brokeFree));
}

// gd: L59-L60
void DealerIntelligence::Animator_CheckHandcuffs() {
    speaker_checkHandcuffs->play();
}

// gd: L62-L63
void DealerIntelligence::Animator_GiveHandcuffs() {
    speaker_giveHandcuffs->play();
}

// gd: L65-L69
void DealerIntelligence::BeginDealerTurn() {
    mainLoopFinished = false;
    usingHandsaw = false;
    usingMedicine = false;
    get_tree()->spawn(DealerChoice());   // called without await in the .gd
}

// gd: L85-L280
seq::Task DealerIntelligence::DealerChoice() {
    std::string dealerWantsToUse;
    bool dealerFinishedUsingItems = false;
    bool hasHandsaw = false;
    bool hasCigs = false;
    if (roundManager->requestedWireCut) co_await roundManager->defibCutter->CutWire(roundManager->wireToCut);
    if (shellSpawner->sequenceArray.empty()) {
        get_tree()->spawn(roundManager->StartRound(true));
        co_return;
    }

    if (roundManager->endless && !dealerKnowsShell) {
        dealerKnowsShell = FigureOutShell();
        if (dealerKnowsShell) {
            if (roundManager->shellSpawner->sequenceArray[0] == "blank") {
                knownShell = "blank";
                dealerTarget = "self";
            } else {
                knownShell = "live";
                dealerTarget = "player";
            }
        }
    }

    if (shellSpawner->sequenceArray.size() == 1) {
        knownShell = shellSpawner->sequenceArray[0];
        if (shellSpawner->sequenceArray[0] == "live") {
            knownShell = "live";
        } else {
            knownShell = "blank";
        }
        if (knownShell == "live") {
            dealerTarget = "player";
        } else {
            dealerTarget = "self";
        }
        dealerKnowsShell = true;
    }
    for (const std::string& item : itemManager->itemArray_dealer) {
        if (item == "cigarettes") {
            hasCigs = true;
            break;
        }
    }

    inv_playerside.clear();
    inv_dealerside.clear();
    itemManager->itemArray_dealer.clear();
    itemManager->itemArray_instances_dealer.clear();
    bool usingAdrenaline = false;
    const int count = itemManager->itemSpawnParent->get_child_count();
    auto itemAt = [&](int c) { return godot::cast_to<godot::Node3D>(itemManager->itemSpawnParent->get_child(c)); };
    auto indicatorOf = [](godot::Node3D* n) { return godot::script_cast<PickupIndicator>(n->get_child(0)); };
    auto branchOf = [](godot::Node3D* n) { return godot::script_cast<InteractionBranch>(n->get_child(1)); };
    for (int c = 0; c < count; ++c) {
        godot::Node3D* ch = itemAt(c);
        if (!indicatorOf(ch)) continue;
        InteractionBranch* temp_interaction = branchOf(ch);
        if (temp_interaction->itemName == "adrenaline" && !temp_interaction->isPlayerSide) {
            usingAdrenaline = true;
            adrenalineSetup = true;
        }
    }
    for (int c = 0; c < count; ++c) {
        godot::Node3D* ch = itemAt(c);
        PickupIndicator* temp_indicator = indicatorOf(ch);
        if (!temp_indicator) continue;
        InteractionBranch* temp_interaction = branchOf(ch);
        temp_indicator->whichSide = ch->get_position().z > 0 ? "right" : "left";
        if (!temp_interaction->isPlayerSide) {
            inv_dealerside.push_back(temp_interaction->itemName);
            itemManager->itemArray_dealer.push_back(temp_interaction->itemName);
            itemManager->itemArray_instances_dealer.push_back(ch);
        }
    }
    for (int c = 0; c < count; ++c) {
        godot::Node3D* ch = itemAt(c);
        PickupIndicator* temp_indicator = indicatorOf(ch);
        if (!temp_indicator) continue;
        InteractionBranch* temp_interaction = branchOf(ch);
        temp_indicator->whichSide = ch->get_position().z > 0 ? "right" : "left";
        if (temp_interaction->isPlayerSide && usingAdrenaline) {
            itemManager->itemArray_dealer.push_back(temp_interaction->itemName);
            itemManager->itemArray_instances_dealer.push_back(ch);
            inv_playerside.push_back(temp_interaction->itemName);
        }
    }

    std::vector<std::string>& seq = shellSpawner->sequenceArray;
    for (const std::string& item : itemManager->itemArray_dealer) {
        if (item == "magnifying glass" && !dealerKnowsShell && seq.size() != 1) {
            dealerWantsToUse = "magnifying glass";
            knownShell = seq[0] == "live" ? "live" : "blank";
            dealerTarget = knownShell == "live" ? "player" : "self";
            dealerKnowsShell = true;
            break;
        }
        if (item == "cigarettes") {
            if (roundManager->health_opponent < roundManager->roundArray[0]->startingHealth) {
                dealerWantsToUse = "cigarettes";
                hasCigs = false;
                break;
            }
        }
        if (item == "expired medicine" && roundManager->health_opponent < roundManager->roundArray[0]->startingHealth &&
            !hasCigs && !usingMedicine) {
            if (roundManager->health_opponent != 1) {
                dealerWantsToUse = "expired medicine";
                usingMedicine = true;
                break;
            }
        }
        if (item == "beer" && knownShell != "live" && seq.size() != 1) {
            dealerWantsToUse = "beer";
            shellEject_dealer->FadeOutShell();
            if (roundManager->endless) {
                dealerKnowsShell = false;
                knownShell = "";
            }
            break;
        }
        if (item == "handcuffs" && roundManager->playerCuffed == false && seq.size() != 1) {
            dealerWantsToUse = "handcuffs";
            roundManager->playerCuffed = true;
            break;
        }
        if (item == "handsaw" && !roundManager->barrelSawedOff && knownShell == "live") {
            dealerWantsToUse = "handsaw";
            usingHandsaw = true;
            roundManager->barrelSawedOff = true;
            roundManager->currentShotgunDamage = 2;
            break;
        }
        if (item == "burner phone" && seq.size() > 2) {
            const int len = static_cast<int>(seq.size());
            int randindex = static_cast<int>(godot::randi_range(1, len - 1));
            if (randindex == 8) randindex -= 1;
            sequenceArray_knownShell[static_cast<std::size_t>(randindex)] = true;
            dealerWantsToUse = "burner phone";
            break;
        }
        if (item == "inverter" && dealerKnowsShell && knownShell == "blank") {
            dealerWantsToUse = "inverter";
            knownShell = "live";
            dealerKnowsShell = true;
            seq[0] = "live";
            dealerTarget = "player";
            break;
        }
    }

    if (dealerWantsToUse.empty()) mainLoopFinished = true;
    for (const std::string& item : itemManager->itemArray_dealer) {
        if (item == "handsaw") hasHandsaw = true;
    }
    if (mainLoopFinished && !usingHandsaw && hasHandsaw && !roundManager->barrelSawedOff && knownShell != "blank") {
        const int decision = CoinFlip();
        if (decision == 0) {
            dealerTarget = "self";
        } else {
            dealerUsedItem = true;
            dealerTarget = "player";
            dealerWantsToUse = "handsaw";
            usingHandsaw = true;
            roundManager->barrelSawedOff = true;
            roundManager->currentShotgunDamage = 2;
        }
    }

    if (!dealerWantsToUse.empty()) {
        if (dealerHoldingShotgun) {
            animator_shotgun->play("enemy put down shotgun");
            shellLoader->DealerHandsDropShotgun();
            dealerHoldingShotgun = false;
            co_await get_tree()->create_timer(.45);
        }
        dealerUsedItem = true;
        if (roundManager->waitingForDealerReturn) {
            co_await get_tree()->create_timer(1.8);
            roundManager->waitingForDealerReturn = false;
        }

        bool returning = false;
        if (dealerWantsToUse == "expired medicine") {
            const double medicine_outcome = godot::randf_range(0.0, 1.0);
            const bool dying = !(medicine_outcome < .5);
            medicine->dealerDying = dying;
            returning = true;
        }
        for (AmountResource* res : amounts->array_amounts) {
            if (dealerWantsToUse == res->itemName) {
                res->amount_dealer -= 1;
                break;
            }
        }

        bool stealingFromPlayer = true;
        for (const std::string& item : inv_dealerside) {
            if (item == dealerWantsToUse) stealingFromPlayer = false;
        }
        bool subtracting = true;
        bool temp_stealing = false;
        // The awaited pickup erases from itemArray_instances_dealer: nothing may
        // touch the loop after the co_await but the `break`, as in the .gd.
        for (godot::Node3D* inst : itemManager->itemArray_instances_dealer) {
            InteractionBranch* b = branchOf(inst);
            if (b->itemName == dealerWantsToUse && b->isPlayerSide && dealerWantsToUse != "adrenaline" && adrenalineSetup &&
                stealingFromPlayer) {
                temp_stealing = true;
                co_await hands->PickupItemFromTable("adrenaline");
                itemManager->numberOfItemsGrabbed_enemy -= 1;
                subtracting = false;
                adrenalineSetup = false;
                break;
            }
        }

        if (temp_stealing) hands->stealing = true;
        co_await hands->PickupItemFromTable(dealerWantsToUse);
        // gd: additional delay for the health update routine (cigarettes only).
        if (dealerWantsToUse == "cigarettes") co_await get_tree()->create_timer(1.1);
        const auto it = std::find(itemManager->itemArray_dealer.begin(), itemManager->itemArray_dealer.end(), dealerWantsToUse);
        if (it != itemManager->itemArray_dealer.end()) itemManager->itemArray_dealer.erase(it);   // gd: Array.erase
        if (subtracting) itemManager->numberOfItemsGrabbed_enemy -= 1;

        if (returning) co_return;

        get_tree()->spawn(DealerChoice());   // called without await in the .gd
        co_return;
    }
    if (dealerWantsToUse.empty()) dealerFinishedUsingItems = true;
    if (roundManager->waitingForDealerReturn) co_await get_tree()->create_timer(1.8);
    if (!dealerHoldingShotgun && dealerFinishedUsingItems) {
        get_tree()->spawn(GrabShotgun());   // called without await in the .gd
        co_await get_tree()->create_timer(1.4 + .5 - 1);
    }
    co_await get_tree()->create_timer(1);
    if (dealerTarget.empty()) {
        ChooseWhoToShootRandomly();
    } else {
        get_tree()->spawn(Shoot(dealerTarget));
    }
    dealerTarget = "";
    knownShell = "";
    dealerKnowsShell = false;
}

// gd: L305-L324
void DealerIntelligence::EndDealerTurn(bool canDealerGoAgain) {
    dealerCanGoAgain = canDealerGoAgain;
    // USINGITEMS: ASSIGN DEALER CAN GO AGAIN FROM ITEMS HERE
    // CHECK IF OUT OF HEALTH
    const bool outOfHealth_player = roundManager->health_player == 0;
    const bool outOfHealth_enemy = roundManager->health_opponent == 0;
    const bool outOfHealth = outOfHealth_player || outOfHealth_enemy;
    if (outOfHealth) {
        // gd: the OutOfHealth("player") line here is commented out.
        if (outOfHealth_enemy) get_tree()->spawn(roundManager->OutOfHealth("dealer"));
        return;
    }
    if (!dealerCanGoAgain) {
        get_tree()->spawn(EndTurnMain());
    } else {
        if (!shellSpawner->sequenceArray.empty()) {
            BeginDealerTurn();
        } else {
            get_tree()->spawn(EndTurnMain());
        }
    }
}

// gd: L326-L329
void DealerIntelligence::ChooseWhoToShootRandomly() {
    const int decision = CoinFlip();
    if (decision == 0) {
        get_tree()->spawn(Shoot("self"));
    } else {
        get_tree()->spawn(Shoot("player"));
    }
}

// gd: L331-L339
seq::Task DealerIntelligence::GrabShotgun() {
    // gd: the camera lerp above is commented out in the original.
    co_await shellLoader->DealerHandsGrabShotgun();
    co_await get_tree()->create_timer(.2);
    animator_shotgun->play("grab shotgun_pointing enemy");
    dealerHoldingShotgun = true;
}

// gd: L341-L348
seq::Task DealerIntelligence::EndTurnMain() {
    co_await get_tree()->create_timer(.5);
    camera->BeginLerp("home");
    if (dealerHoldingShotgun) {
        animator_shotgun->play("enemy put down shotgun");
        shellLoader->DealerHandsDropShotgun();
    }
    dealerHoldingShotgun = false;
    get_tree()->spawn(roundManager->EndTurn(true));
}

// gd: L350-L394
seq::Task DealerIntelligence::Shoot(std::string who) {
    const std::string currentRoundInChamber = shellSpawner->sequenceArray[0];
    dealerCanGoAgain = false;
    bool playerDied = false;
    // gd: `dealerDied` is declared and never read in the original.
    ejectManager->FadeOutShell();
    // gd: #ANIMATION DEPENDING ON WHO IS SHOT
    if (who == "self") {
        co_await get_tree()->create_timer(.2);
        animator_shotgun->play("enemy shoot self");
        co_await get_tree()->create_timer(2);
        shotgunShooting->whoshot = "dealer";
        shotgunShooting->PlayShootingSound();
    } else if (who == "player") {
        animator_shotgun->play("enemy shoot player");
        co_await get_tree()->create_timer(2);
        shotgunShooting->whoshot = "player";
        shotgunShooting->PlayShootingSound();
    }
    // SUBTRACT HEALTH. ASSIGN DEALER CAN GO AGAIN. RETURN IF DEAD
    if (currentRoundInChamber == "live" && who == "self") {
        roundManager->health_opponent -= roundManager->currentShotgunDamage;
        if (roundManager->health_opponent < 0) roundManager->health_opponent = 0;
        smoke->SpawnSmoke("barrel");
        cameraShaker->Shake();
        dealerCanGoAgain = false;
        get_tree()->spawn(death->Kill("dealer", false, true));   // not awaited in the .gd
        co_return;
    }
    if (currentRoundInChamber == "live" && who == "player") {
        roundManager->health_player -= roundManager->currentShotgunDamage;
        if (roundManager->health_player < 0) roundManager->health_player = 0;
        cameraShaker->Shake();
        smoke->SpawnSmoke("barrel");
        co_await death->Kill("player", false, false);            // awaited
        playerDied = true;
    }
    if (currentRoundInChamber == "blank" && who == "self") dealerCanGoAgain = true;
    (void)playerDied;   // gd: assigned and never read
    // EJECTING SHELLS
    co_await get_tree()->create_timer(.4);
    if (who == "player") animator_shotgun->play("enemy eject shell_from player");
    if (who == "self") animator_shotgun->play("enemy eject shell_from self");
    co_await get_tree()->create_timer(1.7);
    // gd: the sequenceArray.remove_at(0) here is commented out; the ejection owns it.
    EndDealerTurn(dealerCanGoAgain);
}

// gd: L421-L431. Outside endless this is a true 50/50, as in the original: the
// probability weighting below is gated on roundManager.endless (Double or
// Nothing only). Do not "improve" the base-game dealer.
int DealerIntelligence::CoinFlip() {
    int result = 0;
    if (!roundManager->endless) {
        result = static_cast<int>(godot::randi_range(0, 1));
    } else {
        const auto& seq = shellSpawner->sequenceArray;
        const auto c_live = std::count(seq.begin(), seq.end(), std::string("live"));
        const auto c_blank = std::count(seq.begin(), seq.end(), std::string("blank"));
        if (c_live == c_blank) result = static_cast<int>(godot::randi_range(0, 1));
        if (c_live > c_blank) result = 1;
        if (c_live < c_blank) result = 0;
    }
    return result;
}

// gd: L282-L303
bool DealerIntelligence::FigureOutShell() {
    // gd: an index past the end is a script error, and the call returns null
    // (false). PORT: guarded here instead of reading past the vector.
    if (sequenceArray_knownShell.empty()) return false;
    if (sequenceArray_knownShell[0] == true) return true;

    const auto& seq = shellSpawner->sequenceArray;
    const auto& mem = sequenceArray_knownShell;

    int c_live = 0;
    int c_blank = 0;
    for (const std::string& shell : seq) {
        if (shell == "blank") c_blank += 1;
        if (shell == "live") c_live += 1;
    }
    if (c_live == 0) return true;
    if (c_blank == 0) return true;

    for (std::size_t c = 0; c < mem.size(); ++c) {
        if (mem[c] == true) {
            if (c >= seq.size()) return false;   // PORT: as above
            if (seq[c] == "live") c_live -= 1;
            else c_blank -= 1;
        }
    }
    if (c_live == 0) return true;
    if (c_blank == 0) return true;

    return false;
}

// gd: L414-L419
void DealerIntelligence::SwapDealerMesh() {
    if (!swapped) {
        dealermesh_normal->set_layer_mask_value(1, false);
        dealermesh_crushed->set_layer_mask_value(1, true);
        swapped = true;
    }
}

}  // namespace scripts
