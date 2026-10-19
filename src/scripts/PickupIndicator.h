// source: scripts/PickupIndicator.gd (133 lines)
// PORT deviations:
//   - _ready's absolute /root/main/... paths resolve from the scene root.
//   - AudioStream exports are their resource paths.
#pragma once

#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>
#include <vector>

namespace scripts {

class PickupIndicator : public godot::Script {
public:
    static constexpr const char* kClassName = "PickupIndicator";
    const char* get_class_name() const override { return kClassName; }

    DescriptionManager* description = nullptr;
    InteractionManager* interaction = nullptr;
    godot::Node3D* obj = nullptr;
    godot::Node3D* collider = nullptr;
    double dur = 0.0;
    double axisOffset = 0.0;
    double originalAxis = 0.0;
    std::string activeAxis;
    std::vector<godot::Label*> uiArray;
    std::string itemName;
    std::string itemDescription;
    std::string whichSide;
    int dealerGridIndex = 0;
    std::string sound_placeDown;
    std::string sound_indicatorPickup;
    std::string sound_indicatorPutdown;
    std::string sound_pickup;

    godot::AudioStreamPlayer* speaker_indicatorCheck = nullptr;
    int locationInDynamicArray = 0;
    ItemManager* itemManager = nullptr;
    bool isDealerItem = false;
    // gd: an untyped `var` that ItemManager.GrabItems_Enemy fills with the dealer
    // GRID NODE the item sits on (ItemManager.gd line 391), and HandManager hands back to
    // gridParentArray_enemy_available when the item leaves it. A node, not a name.
    godot::Node3D* dealerGridName = nullptr;
    bool lerpEnabled = true;
    bool interactionAllowed = false;
    bool interactionInvalid = false;
    double current = 0.0;
    double next = 0.0;
    double elapsed = 0.0;
    bool moving = false;
    bool fs = false;
    bool fs2 = false;

    void _ready() override;                          // gd: L38-L42
    bool has_process() const override { return true; }
    void _process(double delta) override;            // gd: L44-L47
    void CheckIfActive();                            // gd: L49-L61
    void BeginPickup();                              // gd: L63-L76
    void Revert();                                   // gd: L78-L91
    void LerpMovement();                             // gd: L93-L104
    void SnapToMax();                                // gd: L106-L113
    void SnapToMin();                                // gd: L115-L122
};

}  // namespace scripts
