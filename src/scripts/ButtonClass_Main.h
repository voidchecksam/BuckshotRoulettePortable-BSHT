// source: scripts/ButtonClass_Main.gd (55 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>

namespace scripts {

class ButtonClass_Main : public godot::Script {
public:
    static constexpr const char* kClassName = "ButtonClass_Main";
    const char* get_class_name() const override { return kClassName; }

    std::string alias;
    std::string alias_signature;
    SignButton* signatureBranch = nullptr;
    bool isSignature = false;
    bool usingInteractionPipe = false;
    InteractionManager* interaction = nullptr;
    bool isActive = false;
    bool isDynamic = false;
    godot::Control* ui = nullptr;
    godot::GeometryInstance3D* ui_3D = nullptr;
    bool is3D = false;
    double ui_opacity_inactive = 0.0;
    double ui_opacity_active = 0.0;
    godot::Node* signature = nullptr;  // PORT: Signature not translated yet
    bool overridingMouseRaycast = false;
    MouseRaycast* mouseRaycast = nullptr;
    godot::Vector2 mouseRaycastVector;
    bool usingInteractionBranch = false;
    bool mainActive = true;

    void _ready() override;       // gd: L23-L28
    void SetUI(bool state);       // gd: L30-L36
    void OnHover();               // gd: L38-L42
    void OnExit();                // gd: L44-L47
    void OnPress();               // gd: L50-L55
};

}  // namespace scripts
