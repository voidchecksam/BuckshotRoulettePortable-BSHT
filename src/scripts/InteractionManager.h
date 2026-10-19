// source: scripts/InteractionManager.gd (133 lines)
// PORT deviations:
//   - port_interactions records every InteractWith alias in order (tests and
//     the --debug log read it). Nothing in the game logic reads it.
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

class InteractionManager : public godot::Script {
public:
    static constexpr const char* kClassName = "InteractionManager";
    const char* get_class_name() const override { return kClassName; }

    Signature* sign = nullptr;
    BriefcaseMachine* brief = nullptr;
    Heaven* heaven = nullptr;
    IntroManager* intro = nullptr;
    CursorManager* cursor = nullptr;
    MouseRaycast* mouseRay = nullptr;
    ShotgunShooting* shotgun = nullptr;
    DecisionTextManager* decision = nullptr;
    ItemManager* itemManager = nullptr;
    ItemInteraction* itemInteraction = nullptr;
    CRT* crt = nullptr;
    godot::Node* activeParent = nullptr;
    InteractionBranch* activeInteractionBranch = nullptr;
    bool checking = true;

    std::vector<std::string> port_interactions;

    bool has_process() const override { return true; }
    void _process(double delta) override;                                    // gd: L18-L22
    bool has_input() const override { return true; }
    void _input(const godot::InputEventRef& event) override;                 // gd: L24-L26
    void MainInteractionEvent();                                             // gd: L28-L33
    void CheckIfHovering();                                                  // gd: L35-L42
    bool fs_dec = false;   // one shot per decision (v2.2)
    void InteractWith(const std::string& alias);                             // gd: L45-L96
    void SignatureButtonRemote(SignButton* assignedBranch, const std::string& alias);  // gd: L98-L101
    void InteractWithGrid(int tempGridIndex);                                // gd: L103-L105
    void CheckPickupLerp();                                                  // gd: L107-L118
    void CheckInteractionBranch();                                           // gd: L120-L133
};

}  // namespace scripts
