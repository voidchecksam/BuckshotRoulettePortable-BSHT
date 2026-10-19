#include "scripts/SignatureManager.h"

#include "godot/array_ops.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/AchievementManager.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/RoundManager.h"
#include "scripts/resources.h"

namespace scripts {

void Signature::Spawn(seq::Task&& task) {
    get_tree()->spawn(std::move(task), get_node_owner());
}

// gd: L33-L38
void Signature::_ready() {
    Spawn(ReadyRoutine());
}

seq::Task Signature::ReadyRoutine() {
    for (godot::Label3D* l : letterArray_signature_joined) l->set_text("");
    for (godot::Label3D* l : letterArray_signature_separate) l->set_text("");
    parent_signatureMachineMainParent->set_visible(false);
    co_await get_tree()->create_timer(2);
    if (roundManager->playerData->hasSignedWaiver == false) SetupWaiver();
}

// gd: L40-L47
seq::Task Signature::BlinkMarker() {
    while (blinking) {
        godot::Color c = marker->get_modulate();
        c.a = 0;
        marker->set_modulate(c);
        co_await get_tree()->create_timer(.3);
        c.a = 1;
        marker->set_modulate(c);
        co_await get_tree()->create_timer(.3);
    }
}

// gd: L49-L52
void Signature::SetupWaiver() {
    origpos_shotgun = parent_shotgun->get_position();
    parent_shotgun->set_transform_origin({-1.738f, -5.0f, -0.041f});
    parent_waiver->set_transform_origin({-29.613f, -35.341f, -6.696f});
}

// gd: L54-L60
seq::Task Signature::AwaitPickup() {
    co_await get_tree()->create_timer(.6);
    cursor->SetCursor(true, true);
    intrbranch_waiver->interactionAllowed = true;
    btn_waiver->set_visible(true);
    if (cursor->controller_active) btn_waiver->grab_focus();
    controller->previousFocus = btn_waiver;
}

// gd: L62-L79
seq::Task Signature::PickUpWaiver() {
    speaker_bootup->play();
    parent_signatureMachineMainParent->set_visible(true);
    intrbranch_waiver->interactionAllowed = false;
    btn_waiver->set_visible(false);
    cursor->SetCursor(false, false);
    anim_waiver->play("pickup waiver");
    for (godot::Label3D* letter : letterArray) letter->set_text("");
    UpdateMarkerPosition();
    UpdateLEDArray();
    blinking = true;
    Spawn(BlinkMarker());
    co_await get_tree()->create_timer(2.77);   // gd: #.9 anim speed
    for (InteractionBranch* intbr : intbranches) intbr->interactionAllowed = true;
    cursor->SetCursor(true, true);
    btnParent_signature->set_visible(true);
    if (cursor->controller_active) btn_signature_a->grab_focus();
    controller->previousFocus = btn_signature_a;
}

// gd: L81-L92
void Signature::GetInput(const std::string& letterAlias, const std::string& specialAlias) {
    speaker_keypress->set_pitch_scale(static_cast<float>(godot::randf_range(.95, 1)));
    speaker_keypress->play();
    std::string input;
    if (letterAlias != "") {
        input = letterAlias;
    } else {
        input = specialAlias;
    }
    if (input != "backspace" && input != "enter") {
        Input_Letter(input);
    } else if (input == "enter") {
        Spawn(Input_Enter());
    } else if (input == "backspace") {
        Input_Backspace();
    }
}

// gd: L94-L100
void Signature::Input_Letter(const std::string& alias) {
    if (markerIndex != 6) {
        if (letterArray[static_cast<std::size_t>(markerIndex)]->get_text() == "") {
            letterArray[static_cast<std::size_t>(markerIndex)]->set_text(alias);
        }
    }
    if (markerIndex != 6) markerIndex += 1;
    UpdateMarkerPosition();
    UpdateLEDArray();
}

// gd: L103-L145
seq::Task Signature::Input_Enter() {
    std::vector<std::string> chararray;
    fullstring = "";
    for (godot::Label3D* letter : letterArray) {
        if (letter->get_text() != "") chararray.push_back(letter->get_text());
    }
    for (const std::string& l : chararray) fullstring += l;
    lettercount = static_cast<int>(chararray.size());
    if (fullstring == "") co_return;
    if (fullstring == "dealer") co_return;
    if (fullstring == "god") {
        if (ach) ach->UnlockAchievement("ach11");
        co_return;
    }
    if (fullstring != "") {
        for (InteractionBranch* br : intbranches) {
            auto* el = godot::cast_to<godot::CollisionObject3D>(br->get_node_owner()->get_parent()->get_child(2));
            br->interactionAllowed = false;
            if (el) {
                el->set_collision_layer_value(1, false);
                el->set_collision_mask_value(1, false);
            }
        }
    }
    cursor->SetCursor(false, false);
    btnParent_signature->set_visible(false);
    co_await get_tree()->create_timer(.25);
    for (int i = 0; i < lettercount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        letterArray_signature_joined[k]->set_text(godot::to_upper(chararray[k]));
        letterArray_signature_separate[k]->set_text(godot::to_upper(chararray[k]));
        ledArray[k]->set_visible(false);
        co_await get_tree()->create_timer(.17);
        speaker_punch->set_pitch_scale(static_cast<float>(godot::randf_range(.95, 1)));
        speaker_punch->play();
    }
    roundManager->counting = true;
    co_await get_tree()->create_timer(.17);
    parent_shotgun->set_transform_origin(origpos_shotgun);
    anim_waiver->play("put away waiver");
    speaker_bootup->stop();
    speaker_shutdown->play();
    roundManager->playerData->playername = " " + fullstring;
    roundManager->playerData->hasSignedWaiver = true;
    Spawn(ReturnToMainBatch());
    co_await get_tree()->create_timer(1.72);
    parent_signatureMachineMainParent->set_visible(false);
    parent_separateWaiver->set_visible(false);
    co_await get_tree()->create_timer(.4);
    parent_waiver->queue_free();
}

// gd: L147-L150
seq::Task Signature::ReturnToMainBatch() {
    co_await get_tree()->create_timer(1.27);
    roundManager->enteringFromWaiver = true;
    get_tree()->spawn(roundManager->MainBatchSetup(false), roundManager->get_node_owner());
}

// gd: L152-L158
void Signature::Input_Backspace() {
    if (markerIndex != 6 && markerIndex != 0) letterArray[static_cast<std::size_t>(markerIndex - 1)]->set_text("");
    if (markerIndex == 6) letterArray[static_cast<std::size_t>(markerIndex - 1)]->set_text("");
    if (markerIndex != 0) markerIndex -= 1;
    UpdateMarkerPosition();
    UpdateLEDArray();
}

// gd: L160-L164
void Signature::UpdateMarkerPosition() {
    if (markerIndex != 6) marker->set_transform_origin(posArray_marker[static_cast<std::size_t>(markerIndex)]);
    if (markerIndex == 5 && letterArray[static_cast<std::size_t>(markerIndex)]->get_text() != "") {
        marker->set_visible(false);
    } else if (markerIndex == 6) {
        marker->set_visible(false);
    } else {
        marker->set_visible(true);
    }
}

// gd: L166-L171
void Signature::UpdateLEDArray() {
    for (std::size_t i = 0; i < ledArray.size(); ++i) {
        ledArray[i]->set_transparency(letterArray[i]->get_text() != "" ? 0.0f : 1.0f);
    }
}

}  // namespace scripts
