// source: scripts/InteractionBranch.gd (20 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>

namespace scripts {

class InteractionBranch : public godot::Script {
public:
    static constexpr const char* kClassName = "InteractionBranch";
    const char* get_class_name() const override { return kClassName; }

    std::string interactionAlias;
    std::string itemName;
    bool interactionAllowed = false;
    bool isGrid = false;
    int gridIndex = 0;
    bool isPlayerSide = false;
    int itemGridIndex = 0;
    bool interactionInvalid = false;
    std::string signatureButton_letterAlias;
    std::string signatureButton_specialAlias;
    std::string crtButton_alias;
    SignButton* assignedSignatureButton = nullptr;

    void _ready() override;   // gd: L16-L20
};

}  // namespace scripts
