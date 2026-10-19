// source: scripts/PermissionManager.gd (83 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>
#include <vector>

namespace scripts {

class PermissionManager : public godot::Script {
public:
    static constexpr const char* kClassName = "PermissionManager";
    const char* get_class_name() const override { return kClassName; }

    RoundManager* roundManager = nullptr;
    ItemManager* itemManager = nullptr;
    std::vector<PickupIndicator*> indicatorArray;
    std::vector<InteractionBranch*> interactionBranchArray;
    DescriptionManager* description = nullptr;
    godot::Node3D* userItemsParent = nullptr;
    std::vector<std::string> stackDisabledItemArray;
    std::vector<bool> stackDisabledItemArray_bools;
    ItemInteraction* inter = nullptr;

    void _ready() override;                          // gd: L12-L14
    void SetIndicators(bool state);                  // gd: L16-L23
    void DisableShotgun();                           // gd: L25-L33
    void SetInteractionPermissions(bool state);      // gd: L36-L51
    void SetItemInteraction(bool state);             // gd: L53-L62
    void SetStackInvalidIndicators();                // gd: L65-L80
    void RevertDescriptionUI();                      // gd: L82-L83
};

}  // namespace scripts
