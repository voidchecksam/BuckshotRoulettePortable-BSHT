// source: scripts/KeyRebinding.gd (134 lines)
// PORT deviations:
//   - `event.double_click = false` writes the shared event, as in GDScript.
//   - The commented-out prints are not translated.
//   - On the Vita the rebind button is hidden: the host hides it, this script
//     is unchanged.
#pragma once

#include "godot/control.h"
#include "godot/input_event.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class Rebinding : public godot::Script {
public:
    static constexpr const char* kClassName = "Rebinding";
    const char* get_class_name() const override { return kClassName; }

    OptionsManager* options = nullptr;
    godot::Control* mouseblocker = nullptr;
    CursorManager* cursor = nullptr;
    ControllerManager* controller = nullptr;
    std::vector<godot::Label*> rebindKeyArray;
    MenuManager* menu = nullptr;

    godot::Node* globalparent = nullptr;
    std::string previousKey;
    bool waitingForAnyInput = false;
    bool checkingForCancel = false;
    bool cancelAllowed = false;

    void UpdateBindList();                                    // gd: L11-L34
    void GetRebind(godot::Node* parent);                      // gd: L38-L50
    bool has_input() const override { return true; }
    void _input(const godot::InputEventRef& event) override;  // gd: L53-L100
    void SwapBind(godot::InputEventRef firstevent, godot::InputEventRef secondevent, const std::string& action,
                  bool assigningKeyboard, const godot::InputEventRef& original_keyboard,
                  const godot::InputEventRef& original_cntrl);  // gd: L102-L107
    seq::Task ReturnFromRebind();                             // gd: L109-L117
    void CancelRebind();                                      // gd: L120-L129
    bool has_unhandled_input() const override { return true; }
    void _unhandled_input(const godot::InputEventRef& event) override;   // gd: L132-L134
};

}  // namespace scripts
