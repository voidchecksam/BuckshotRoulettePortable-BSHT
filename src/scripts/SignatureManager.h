// source: scripts/SignatureManager.gd (191 lines)
// PORT deviations:
//   - Coroutines keep their names and return seq::Task; a call without await
//     in the .gd is a spawn owned by this node.
//   - `origpos_shotgun` is untyped in the .gd; a Vector3 here.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class Signature : public godot::Script {
public:
    static constexpr const char* kClassName = "Signature";
    const char* get_class_name() const override { return kClassName; }

    RoundManager* roundManager = nullptr;
    godot::Node3D* parent_shotgun = nullptr;
    godot::Node3D* parent_waiver = nullptr;
    godot::Node3D* parent_separateWaiver = nullptr;
    godot::Node3D* parent_signatureMachineMainParent = nullptr;
    InteractionBranch* intrbranch_waiver = nullptr;
    CursorManager* cursor = nullptr;
    godot::AnimationPlayer* anim_waiver = nullptr;
    std::vector<InteractionBranch*> intbranches;
    godot::Label3D* marker = nullptr;
    std::vector<godot::Label3D*> letterArray;
    std::vector<godot::GeometryInstance3D*> ledArray;
    std::vector<godot::Vector3> posArray_marker;
    std::vector<godot::Label3D*> letterArray_signature_joined;
    std::vector<godot::Label3D*> letterArray_signature_separate;
    godot::AudioStreamPlayer* speaker_bootup = nullptr;
    godot::AudioStreamPlayer* speaker_shutdown = nullptr;
    godot::AudioStreamPlayer* speaker_keypress = nullptr;
    godot::AudioStreamPlayer* speaker_punch = nullptr;
    godot::Control* btn_waiver = nullptr;
    godot::Control* btnParent_signature = nullptr;
    godot::Control* btn_signature_a = nullptr;
    ControllerManager* controller = nullptr;
    Achievement* ach = nullptr;

    std::string fullstring;
    int lettercount = 0;
    godot::Vector3 origpos_shotgun;
    int markerIndex = 0;
    bool blinking = false;

    void _ready() override;                      // gd: L33-L38 (spawns ReadyRoutine)
    seq::Task ReadyRoutine();                    // gd: L33-L38
    seq::Task BlinkMarker();                     // gd: L40-L47
    void SetupWaiver();                          // gd: L49-L52
    seq::Task AwaitPickup();                     // gd: L54-L60
    seq::Task PickUpWaiver();                    // gd: L62-L79
    void GetInput(const std::string& letterAlias, const std::string& specialAlias);   // gd: L81-L92
    void Input_Letter(const std::string& alias); // gd: L94-L100
    seq::Task Input_Enter();                     // gd: L103-L145
    seq::Task ReturnToMainBatch();               // gd: L147-L150
    void Input_Backspace();                      // gd: L152-L158
    void UpdateMarkerPosition();                 // gd: L160-L164
    void UpdateLEDArray();                       // gd: L166-L171

private:
    void Spawn(seq::Task&& task);   // a call without await: owned by this node
};

}  // namespace scripts
