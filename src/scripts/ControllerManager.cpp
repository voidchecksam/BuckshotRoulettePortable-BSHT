#include "scripts/ControllerManager.h"

#include "godot/input.h"
#include "godot/scene_tree.h"
#include "scripts/ButtonClass.h"
#include "scripts/CursorManager.h"
#include "scripts/MouseRaycast.h"

namespace scripts {

// gd: L13-L15
void ControllerManager::_ready() {
    get_tree()->connect_gui_focus_changed([this](godot::Control* control) { _on_focus_changed(control); });
    const std::vector<int> controllers = godot::Input::get_singleton().get_connected_joypads();
    (void)controllers;   // the .gd reads it into a local and never uses it
}

// gd: L17-L18
void ControllerManager::_process(double /*delta*/) {
    if (settingVisibility) SetVisibility();
}

// gd: L20-L22
void ControllerManager::_on_focus_changed(godot::Control* control) {
    if (control != nullptr) previousFocus = control;
}

// gd: L25-L36
void ControllerManager::SetMainControllerState(bool controllerActive) {
    if (controllerActive) {
        controller_currently_enabled = true;
        cursor->controller_active = true;
        SetPrevFocus(true);
        cursor->SetCursor(cursor->cursor_visible, false);
    } else {
        controller_currently_enabled = false;
        cursor->controller_active = false;
        SetPrevFocus(false);
        cursor->SetCursor(cursor->cursor_visible, false);
    }
}

// gd: L40-L61
void ControllerManager::_input(const godot::InputEventRef& event) {
    if (dynamicallySwappingDevice && checkingForInput) {
        // ENABLE CONTROLLER
        if (godot::event_cast<godot::InputEventJoypadButton>(event.get())) {
            cursor->controller_active = true;
            SetPrevFocus(true);
            cursor->SetCursor(cursor->cursor_visible, false);
        } else if (godot::event_cast<godot::InputEventKey>(event.get())) {
            if (!IsEventAssignedToNavigation(*event)) return;
            cursor->controller_active = true;
            SetPrevFocus(true);
            cursor->SetCursor(cursor->cursor_visible, false);
        }
        // DISABLE CONTROLLER
        else if (const auto* motion = godot::event_cast<godot::InputEventMouseMotion>(event.get())) {
            if (motion->velocity.x == 0 && motion->velocity.y == 0) return;
            cursor->controller_active = false;
            SetPrevFocus(false);
            cursor->SetCursor(cursor->cursor_visible, false);
        } else if (godot::event_cast<godot::InputEventMouseButton>(event.get())) {
            cursor->controller_active = false;
            SetPrevFocus(false);
            cursor->SetCursor(cursor->cursor_visible, false);
        }
    }
}

// gd: L64-L67
bool ControllerManager::IsEventAssignedToNavigation(const godot::InputEvent& key) {
    for (const std::string& b : navigationBinds) {
        if (key.is_action(b)) return true;
    }
    return false;
}

// gd: L69-L75
void ControllerManager::SetRebindFocus(bool settingToPrevious) {
    // PORT: previousFocus is null until a screen sets it; gd errors on null.
    if (!previousFocus) return;
    if (!settingToPrevious) {
        previousFocus->release_focus();
        return;
    }
    if (cursor->controller_active) {
        if (settingToPrevious) previousFocus->grab_focus();
    }
}

// gd: L81-L94
void ControllerManager::SetPrevFocus(bool grabbing) {
    if (grabbing && !fs1) {
        ExitButtons();
        if (settingFilter) {
            for (ButtonClass* b : buttons) b->SetFilter("ignore");
        }
        if (previousFocus != nullptr) previousFocus->grab_focus();
        fs2 = false;
        fs1 = true;
    } else if (!grabbing && !fs2) {
        ExitButtons();
        if (previousFocus != nullptr) previousFocus->release_focus();
        // PORT: mouseRaycast is null in menu.tscn and heaven.tscn; gd raises a
        // script error there and plays on (the first d-pad press crashed).
        if (stoppingOverride && mouseRaycast) mouseRaycast->StopRaycastOverride();
        if (settingFilter) {
            for (ButtonClass* b : buttons) b->SetFilter("stop");
        }
        fs1 = false;
        fs2 = true;
    }
}

// gd: L96-L98
void ControllerManager::SetVisibility() {
    for (godot::Control* b : brackets) b->set_visible(cursor->controller_active);
    for (godot::Node3D* b : brackets_3d) b->set_visible(cursor->controller_active);
}

// gd: L100-L103
void ControllerManager::ExitButtons() {
    if (exitingButtons) {
        for (ButtonClass* b : buttons) {
            if (!b->resetting) b->OnExit();
        }
    }
}

}  // namespace scripts
