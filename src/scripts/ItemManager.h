// source: scripts/ItemManager.gd (589 lines)
// PORT deviations:
//   - Every function with an `await` is a seq::Task, and a caller that does
//     not await it in the .gd spawns it here (RoundManager, InteractionManager).
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "scripts/resources.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class ItemManager : public godot::Script {
public:
    static constexpr const char* kClassName = "ItemManager";
    const char* get_class_name() const override { return kClassName; }

    GridIndicator* grid = nullptr;
    CursorManager* cursor = nullptr;
    ControllerManager* controller = nullptr;
    RoundManager* roundManager = nullptr;
    Dialogue* dialogue = nullptr;
    CameraManager* camera = nullptr;
    CompartmentManager* comp = nullptr;
    InteractionBranch* interaction_intake = nullptr;
    std::vector<ItemResource*> instanceArray;
    std::vector<ItemResource*> instanceArray_dealer;
    std::vector<std::string> availableItemArray;
    godot::Node3D* itemSpawnParent = nullptr;
    double lerpDuration = 0.0;
    std::vector<godot::Node3D*> gridParentArray;
    std::vector<godot::Node3D*> gridParentArray_enemy;
    std::vector<bool> gridOccupiedArray;
    std::vector<std::string> stringNumberArray;
    godot::Node3D* gridParent_1 = nullptr;
    godot::Node3D* gridParent_2 = nullptr;
    godot::Node3D* gridParent_3 = nullptr;
    godot::Node3D* gridParent_4 = nullptr;
    std::vector<std::string> soundArray_itemGrab;
    godot::AudioStreamPlayer* speaker_itemgrab = nullptr;
    godot::AudioStreamPlayer* speaker_placedown = nullptr;
    godot::AnimationPlayer* anim_spook = nullptr;
    SpeakerController* speakercontroller_musicmain = nullptr;
    godot::AudioStreamPlayer* speaker_god = nullptr;
    Amounts* amounts = nullptr;
    ItemInteraction* interaction = nullptr;
    godot::Control* btnParent_stealing = nullptr;
    godot::Control* btnparent_ff_stealing = nullptr;
    godot::Control* btnParent_intake = nullptr;
    godot::Control* btn_intake = nullptr;
    std::vector<godot::Control*> btn_grids;

    // gd: L35-L64: the plain vars, in source order.
    int itemGrabSoundIndex = 0;
    std::vector<std::string> itemArray_dealer;
    std::vector<godot::Node3D*> itemArray_instances_dealer;
    std::vector<godot::Node3D*> itemArray_instances_dealer_previous;
    std::vector<std::string> itemArray_dealer_previous;
    std::vector<std::string> itemArray_player;
    bool hasBegun = false;
    std::vector<PickupIndicator*> items_dynamicIndicatorArray;
    std::vector<InteractionBranch*> items_dynamicInteractionArray;
    int numberOfCigs_player = 0;
    int numberOfCigs_dealer = 0;
    std::vector<std::string> playerItemStringArray;
    std::vector<std::string> availableItemsToGrabArray_player;
    std::vector<std::string> availableItemsToGrabArray_dealer;
    int numberOfItemsGrabbed_enemy = 0;
    int numberOfItemsGrabbed = 0;
    int numberOfOccupiedGrids = 0;
    godot::Vector3 activeItem_offset_pos;
    godot::Vector3 activeItem_offset_rot;
    godot::Node3D* activeItem = nullptr;
    godot::Node3D* activeItem_enemy = nullptr;
    double elapsed = 0.0;
    bool moving = false;
    godot::Vector3 pos_current;
    godot::Vector3 pos_next;
    godot::Vector3 rot_current;
    godot::Vector3 rot_next;
    PickupIndicator* temp_indicator = nullptr;
    InteractionBranch* temp_interaction = nullptr;
    std::vector<godot::Node3D*> gridParentArray_enemy_available;
    bool newBatchHasBegun = false;       // gd: L116
    bool requestingItemClear = false;    // gd: L117
    int randindex = 4;                   // gd: L210
    int spook_counter = 0;               // gd: L211
    bool spook_fired = false;            // gd: L212
    bool firstItem = true;               // gd: L343
    std::vector<godot::Node3D*> instancesToDelete;  // gd: L540
    // gd: L397-L476: the steal. The .gd spells the first "dyanmic"; kept.
    std::vector<PickupIndicator*> PREVIOUS_items_dyanmicIndicatorArray;
    std::vector<InteractionBranch*> PREVIOUS_items_dynamicInteractionArray;
    double timer_steal_current = 0.0;
    double timer_steal_max = 7.0;
    bool counting = false;
    bool checking = false;
    bool fs = false;

    // Animation METHOD tracks: "clear items" calls ClearAllItems. Without
    // this, the call is dropped silently.
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    void _ready() override;                          // gd: L66-L70
    void _process(double delta) override;            // gd: L72-L75
    bool has_process() const override { return true; }   // the .gd defines _process
    void SetIntakeFocus(bool state);                 // gd: L80-L88
    void ClearIntakeFocus();                         // gd: L91-L94
    seq::Task ItemClear_Remote();                    // gd: L96-L114
    seq::Task BeginItemGrabbing();                   // gd: L118-L174
    seq::Task EndItemGrabbing();                     // gd: L176-L189
    seq::Task GrabSpook();                           // gd: L191-L208
    seq::Task GrabItem();                            // gd: L213-L308
    seq::Task PlaceDownItem(int gridIndex);          // gd: L310-L341
    void GrabItems_Enemy();                          // gd: L344-L394
    seq::Task SetupItemSteal();                      // gd: L399-L432
    void RevertItemSteal();                          // gd: L434-L437
    seq::Task RevertItemSteal_Timeout();             // gd: L439-L455
    void Counter(bool starting);                     // gd: L457-L467
    void ItemStealTimeout();                         // gd: L473-L474
    void CheckTimer();                               // gd: L477-L481
    void ResetDealerGrid();                          // gd: L483-L488
    void SetupItemClear();                           // gd: L490-L538
    void ClearAllItems();                            // gd: L541-L550
    void GridParents(bool setState);                 // gd: L552-L560
    void PlayItemGrabSound();                        // gd: L562-L566
    void LerpItem();                                 // gd: L568-L576
};

}  // namespace scripts
