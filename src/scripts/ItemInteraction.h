// source: scripts/ItemInteraction.gd (242 lines)
// PORT deviations:
//   - PickupItemFromTable and InteractWith are seq::Tasks (they await); a
//     caller that does not await them spawns them.
//   - The achievement checks are omitted: Steam is out of scope.
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

class ItemInteraction : public godot::Script {
public:
    static constexpr const char* kClassName = "ItemInteraction";
    const char* get_class_name() const override { return kClassName; }

    Medicine* medicine = nullptr;
    RoundManager* roundManager = nullptr;
    DealerIntelligence* dealerIntelligence = nullptr;
    CursorManager* cursor = nullptr;
    PermissionManager* perm = nullptr;
    ItemManager* itemManager = nullptr;
    double lerpDuration = 0.0;
    godot::Vector3 pos_hand;
    godot::Vector3 rot_hand;
    godot::Vector3 pos_hand_stealing;
    godot::Vector3 rot_hand_stealing;
    godot::AnimationPlayer* animator_dealerHands = nullptr;
    godot::AnimationPlayer* animator_playerHands = nullptr;
    CameraManager* camera = nullptr;
    ShellEjectManager* shellEject_player = nullptr;
    godot::AudioStreamPlayer* speaker_pickup = nullptr;
    godot::AudioStreamPlayer* speaker_breakcuffs = nullptr;
    godot::AudioStreamPlayer* speaker_interaction = nullptr;
    std::string sound_use_burnerphone;
    std::string sound_use_inverter;
    std::string sound_use_medicine;
    std::string sound_use_adrenaline;
    Amounts* amounts = nullptr;
    ItemManager* items = nullptr;
    HandManager* hands = nullptr;
    Achievement* ach = nullptr;
    godot::AnimationPlayer* anim_pp = nullptr;
    FilterController* filter = nullptr;

    // gd: L15-L16, L32-L40, L49-L50: the plain vars, in source order.
    godot::Vector3 pos_hand_main;
    godot::Vector3 rot_hand_main;
    godot::Node3D* temp_itemParent = nullptr;
    godot::Vector3 pos_current;
    godot::Vector3 pos_next;
    godot::Vector3 rot_current;
    godot::Vector3 rot_next;
    InteractionBranch* temp_interaction = nullptr;
    PickupIndicator* temp_indicator = nullptr;
    double elapsed = 0.0;
    bool moving = false;
    bool stealing = false;
    bool stealing_fs = false;

    // Animation METHOD tracks: "player use adrenaline" calls AdrenalineHit,
    // "player break handcuffs" calls PlaySound_BreakHandcuffs (trap 4).
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    void _ready() override;                                                  // gd: L42-L44
    void _process(double delta) override;                                    // gd: L46-L47
    bool has_process() const override { return true; }                       // the .gd defines _process
    seq::Task PickupItemFromTable(godot::Node3D* itemParent, std::string passedItemName);  // gd: L51-L103
    seq::Task InteractWith(std::string itemName);                            // gd: L105-L184
    void CheckAchievement_koni();   // gd: L187-L188
    void CheckAchievement_full();   // gd: L190-L193
    seq::Task AdrenalineHit();                                               // gd: L197-L201
    void PlaySound(const std::string& clip);                                 // gd: L203-L205
    void EnablePermissions();                                                // gd: L207-L213
    void DisableShotgun();                                                   // gd: L215-L216
    void DisablePermissions();                                               // gd: L218-L223
    void PlaySound_BreakHandcuffs();                                         // gd: L225-L226
    void RemovePlayerItemFromGrid(godot::Node3D* parent);                    // gd: L228-L234
    void LerpMovement();                                                     // gd: L236-L242
};

}  // namespace scripts
