#include "scripts/ButtonClass_Main.h"

#include "scripts/InteractionManager.h"
#include "scripts/MouseRaycast.h"

namespace scripts {

// gd: L23-L28
void ButtonClass_Main::_ready() {
    godot::Node* parent = get_node_owner()->get_parent();
    parent->connect("focus_entered", [this] { OnHover(); });
    parent->connect("focus_exited", [this] { OnExit(); });
    parent->connect("pressed", [this] { OnPress(); });
    if (isDynamic) SetUI(false);
}

// gd: L30-L36
void ButtonClass_Main::SetUI(bool state) {
    if (state) {
        if (!is3D) {
            godot::Color m = ui->get_modulate();
            m.a = static_cast<float>(ui_opacity_active);
            ui->set_modulate(m);
        } else {
            ui_3D->set_visible(true);
        }
    } else {
        if (!is3D) {
            godot::Color m = ui->get_modulate();
            m.a = static_cast<float>(ui_opacity_inactive);
            ui->set_modulate(m);
        } else {
            ui_3D->set_visible(false);
        }
    }
}

// gd: L38-L42
void ButtonClass_Main::OnHover() {
    if (isActive && mainActive) {
        if (isDynamic) {
            SetUI(true);
            if (overridingMouseRaycast) mouseRaycast->GetRaycastOverride(mouseRaycastVector);
        }
    }
}

// gd: L44-L47
void ButtonClass_Main::OnExit() {
    if (isActive && mainActive) {
        if (isDynamic) SetUI(false);
    }
}

// gd: L50-L55
void ButtonClass_Main::OnPress() {
    if (isActive && mainActive) {
        get_node_owner()->emit_signal("is_pressed");
        if (usingInteractionPipe) interaction->InteractWith(alias);
        if (isSignature) interaction->SignatureButtonRemote(signatureBranch, alias_signature);
        if (overridingMouseRaycast) interaction->MainInteractionEvent();
    }
}

}  // namespace scripts
