// source: scripts/ControllerManager.gd (103 lines)
// PORT deviations:
//   - `stoppingOverride` is an untyped @export the binding generator cannot
//     type; it keeps the declared default, which is also main.tscn's value
//     (true, runtime export).
//   - print() calls behind `printing` (false) are dropped.
#pragma once

#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>
#include <vector>

namespace scripts {

class ControllerManager : public godot::Script {
public:
    static constexpr const char* kClassName = "ControllerManager";
    const char* get_class_name() const override { return kClassName; }

    // v2.2
    std::vector<godot::Node3D*> brackets_3d;

    bool dynamicallySwappingDevice = false;
    std::vector<ButtonClass*> buttons;
    std::vector<godot::Control*> brackets;
    CursorManager* cursor = nullptr;
    bool exitingButtons = false;
    bool settingVisibility = false;
    MouseRaycast* mouseRaycast = nullptr;
    godot::Control* previousFocus = nullptr;
    bool controller_currently_enabled = false;
    bool printing = false;
    bool checkingForInput = true;
    std::vector<std::string> navigationBinds{"ui_up", "ui_down", "ui_left", "ui_right"};
    bool fs1 = false;
    bool fs2 = true;
    bool stoppingOverride = true;
    bool settingFilter = false;

    void _ready() override;                                           // gd: L13-L15
    bool has_process() const override { return true; }
    void _process(double delta) override;                             // gd: L17-L18
    void _on_focus_changed(godot::Control* control);                  // gd: L20-L22
    void SetMainControllerState(bool controllerActive);               // gd: L25-L36
    bool has_input() const override { return true; }
    void _input(const godot::InputEventRef& event) override;          // gd: L40-L61
    bool IsEventAssignedToNavigation(const godot::InputEvent& key);   // gd: L64-L67
    void SetRebindFocus(bool settingToPrevious);                      // gd: L69-L75
    void SetPrevFocus(bool grabbing);                                 // gd: L81-L94
    void SetVisibility();                                             // gd: L96-L98
    void ExitButtons();                                               // gd: L100-L103
};

}  // namespace scripts
