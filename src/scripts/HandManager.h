// source: scripts/HandManager.gd (246 lines)
// PORT deviations:
//   - The untyped lerp vars (pos_current...) are Vector3, activeItemToGrab a
//     string, activeHandParent a Node3D*. `elapsed = 0` accumulates seconds:
//     double.
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

class HandManager : public godot::Script {
public:
    static constexpr const char* kClassName = "HandManager";
    const char* get_class_name() const override { return kClassName; }

    DealerIntelligence* dealerAI = nullptr;
    ItemManager* itemManager = nullptr;
    godot::AnimationPlayer* animator_hands = nullptr;
    godot::AnimationPlayer* animator_dealerHeadLook = nullptr;
    std::vector<godot::Node3D*> handArray_L;
    std::vector<godot::Node3D*> handArray_R;
    std::vector<godot::Vector3> gridOffsetArray;
    ItemInteraction* inter = nullptr;
    CameraManager* cam = nullptr;
    godot::Node3D* handParent_L = nullptr;
    godot::Node3D* handParent_R = nullptr;
    godot::Node3D* hand_defaultL = nullptr;
    godot::Node3D* hand_defaultR = nullptr;
    godot::Node3D* hand_cigarettepack = nullptr;
    godot::Node3D* hand_beer = nullptr;
    godot::Node3D* hand_medicine = nullptr;
    godot::Node3D* hand_inverter = nullptr;
    godot::Node3D* hand_adrenaline = nullptr;
    godot::Node3D* hand_handcuffs = nullptr;
    godot::Node3D* hand_handsaw = nullptr;
    godot::Node3D* hand_magnifier = nullptr;
    godot::Node3D* hand_burnerphone = nullptr;
    double lerpDuration = 0.0;
    godot::Vector3 handRot_left;
    godot::Vector3 handRot_right;
    godot::Vector3 parentOriginal_posL;
    godot::Vector3 parentOriginal_rotL;
    godot::Vector3 parentOriginal_posR;
    godot::Vector3 parentOriginal_rotR;
    Amounts* amounts = nullptr;
    godot::AudioStreamPlayer* speaker_interaction = nullptr;
    std::string sound_adrenaline;
    std::string sound_medicine;
    std::string sound_burnerphone;
    std::string sound_inverter;

    bool moving = false;                      // gd: L35
    bool lerping_L = false;                   // gd: L36
    bool lerping_R = false;                   // gd: L37
    godot::Vector3 pos_current;               // gd: L38
    godot::Vector3 pos_next;                  // gd: L39
    godot::Vector3 rot_current;               // gd: L40
    godot::Vector3 rot_next;                  // gd: L41
    double elapsed = 0;                       // gd: L42
    godot::Vector3 orig_pos;                  // gd: L43
    godot::Vector3 orig_rot;                  // gd: L44
    godot::Node3D* activeHandParent = nullptr;   // gd: L45
    std::string activeItemToGrab;             // gd: L46
    bool stealing = false;                    // gd: L51

    bool has_process() const override { return true; }
    void _process(double delta) override;                              // gd: L48-L49
    seq::Task PickupItemFromTable(std::string itemName);               // gd: L52-L129
    void PlaySound(const std::string& itemName);                       // gd: L136-L149
    void RemoveItem_Remote(godot::Node3D* activeInstance);             // gd: L151-L164
    void ToggleHandVisible(const std::string& selectedHand, bool state);   // gd: L166-L174
    void BeginHandLerp(const std::string& whichHand, int gridIndex, const std::string& whichSide);   // gd: L176-L191
    void ReturnHand();                                                 // gd: L193-L199
    void HandFailsafe();                                               // gd: L201-L205
    void LerpHandMovement();                                           // gd: L207-L222
};

}  // namespace scripts
