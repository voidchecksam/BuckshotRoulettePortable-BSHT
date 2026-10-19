#include "scripts/KeyRebinding.h"

#include "godot/input_map.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/KeyRebindBranch.h"
#include "scripts/MenuManager.h"
#include "scripts/OptionsManager.h"

namespace scripts {
namespace {

std::string TrimSuffix(std::string s, const std::string& suffix) {
    if (s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
        s.erase(s.size() - suffix.size());
    }
    return s;
}

std::string RemoveSpaces(std::string s) {
    std::string out;
    for (char c : s) {
        if (c != ' ') out.push_back(c);
    }
    return out;
}

godot::Label* LabelChild(godot::Node* parent, int i) {
    return parent ? godot::cast_to<godot::Label>(parent->get_child(i)) : nullptr;
}

}  // namespace

// gd: L11-L34
void Rebinding::UpdateBindList() {
    godot::InputMap& map = godot::InputMap::get_singleton();
    for (const std::string& action : map.get_actions()) {
        const std::string& action_bindname = action;
        const std::vector<godot::InputEventRef> bindkeys = map.action_get_events(action);
        if (bindkeys.size() < 2) continue;
        std::string bindkey_keyboard = bindkeys[0]->as_text();
        std::string bindkey_controller = bindkeys[1]->as_text();
        for (godot::Label* ui : rebindKeyArray) {
            if (ui && ui->get_name() == action_bindname) {
                godot::Node* parent = ui->get_parent();
                godot::Label* keylabel = LabelChild(parent, 1);
                auto* bindbranch = godot::script_cast<RebindBranch>(parent ? parent->get_child(2) : nullptr);
                if (bindbranch) {
                    bindbranch->originalBind_keyboard = bindkeys[0];
                    bindbranch->originalBind_controller = bindkeys[1];
                }
                const std::size_t firstindex = bindkey_controller.rfind('(');
                bindkey_keyboard = TrimSuffix(bindkey_keyboard, "(Physical)");
                bindkey_keyboard = RemoveSpaces(bindkey_keyboard);
                // String.left(-1) (no "(") keeps all but the last character.
                bindkey_controller = firstindex == std::string::npos
                                         ? bindkey_controller.substr(0, bindkey_controller.empty() ? 0 : bindkey_controller.size() - 1)
                                         : bindkey_controller.substr(0, firstindex);
                // String.replace("Joypad Motion on", ""): only a joypad motion
                // event's text has it, and the game binds none.
                for (std::size_t at; (at = bindkey_controller.find("Joypad Motion on")) != std::string::npos;) {
                    bindkey_controller.erase(at, 16);
                }
                if (keylabel) keylabel->set_text(bindkey_keyboard + ", " + bindkey_controller);
            }
        }
        if (options) options->ParseInputMapDictionary();
    }
}

// gd: L38-L50
void Rebinding::GetRebind(godot::Node* parent) {
    menu->failsafed = false;
    globalparent = parent;
    controller->checkingForInput = false;
    controller->SetRebindFocus(false);
    godot::Label* ui_inputKey = LabelChild(parent, 1);
    previousKey = ui_inputKey ? ui_inputKey->get_text() : std::string();
    mouseblocker->set_visible(true);
    cursor->SetCursor(false, false);
    if (ui_inputKey) ui_inputKey->set_text(godot::tr("PRESS ANY BUTTON REMAP"));
    checkingForCancel = true;
    waitingForAnyInput = true;
}

// gd: L53-L100
void Rebinding::_input(const godot::InputEventRef& event) {
    if (!event) return;
    // PORT: the port raises no InputEventJoypadMotion (the sticks are not read);
    // in gd one is never is_pressed(), so this test is unchanged.
    if (!(waitingForAnyInput && !godot::event_cast<godot::InputEventMouseMotion>(event.get()) && event->is_pressed())) {
        return;
    }
    if (auto* mb = godot::event_cast<godot::InputEventMouseButton>(event.get()); mb && mb->double_click) {
        mb->double_click = false;
    }
    const godot::InputEventRef newEventToBind = event;
    godot::Node* parent = globalparent;
    if (!parent) return;
    const std::string ui_inputName = parent->get_child(0) ? parent->get_child(0)->get_name() : std::string();
    auto* branch = godot::script_cast<RebindBranch>(parent->get_child(2));
    if (!branch) return;
    const godot::InputEventRef originalkey_keyboard = branch->originalBind_keyboard;
    const godot::InputEventRef originalkey_controller = branch->originalBind_controller;
    const bool assigningKeyboard = !godot::event_cast<godot::InputEventJoypadButton>(event.get());
    godot::InputEventRef firstEvent = originalkey_keyboard;
    godot::InputEventRef secondEvent = originalkey_controller;
    if (assigningKeyboard) firstEvent = newEventToBind;
    else secondEvent = newEventToBind;
    godot::InputMap& map = godot::InputMap::get_singleton();
    map.action_erase_events(ui_inputName);
    if (firstEvent) map.action_add_event(ui_inputName, firstEvent);
    if (secondEvent) map.action_add_event(ui_inputName, secondEvent);

    // CHECK IF OVERWRITING KEY
    const std::string currentAction = ui_inputName;
    const godot::InputEventRef currentBind = newEventToBind;
    const std::size_t bindIndex = assigningKeyboard ? 0 : 1;
    for (const std::string& action : map.get_actions()) {
        if (action == currentAction) continue;
        const std::vector<godot::InputEventRef> bindkeys = map.action_get_events(action);
        if (bindkeys.size() < 2) continue;
        if (bindkeys[bindIndex]->as_text() == currentBind->as_text()) {
            SwapBind(bindkeys[0], bindkeys[1], action, assigningKeyboard, originalkey_keyboard,
                     originalkey_controller);
        }
    }
    UpdateBindList();
    get_tree()->spawn(ReturnFromRebind());
}

// gd: L102-L107
void Rebinding::SwapBind(godot::InputEventRef firstevent, godot::InputEventRef secondevent,
                         const std::string& action, bool assigningKeyboard,
                         const godot::InputEventRef& original_keyboard, const godot::InputEventRef& original_cntrl) {
    godot::InputMap& map = godot::InputMap::get_singleton();
    map.action_erase_events(action);
    if (assigningKeyboard) firstevent = original_keyboard;
    else secondevent = original_cntrl;
    if (firstevent) map.action_add_event(action, firstevent);
    if (secondevent) map.action_add_event(action, secondevent);
}

// gd: L109-L117
seq::Task Rebinding::ReturnFromRebind() {
    waitingForAnyInput = false;
    co_await get_tree()->create_timer(.1);
    controller->checkingForInput = true;
    controller->SetRebindFocus(true);
    mouseblocker->set_visible(false);
    cursor->SetCursor(true, false);
    menu->failsafed = true;
}

// gd: L120-L129
void Rebinding::CancelRebind() {
    if (godot::Label* key = LabelChild(globalparent, 1)) key->set_text(previousKey);
    controller->checkingForInput = true;
    controller->SetRebindFocus(true);
    mouseblocker->set_visible(false);
    cursor->SetCursor(true, false);
    checkingForCancel = false;
    menu->failsafed = true;
}

// gd: L132-L134
void Rebinding::_unhandled_input(const godot::InputEventRef& event) {
    if (event && event->is_action_pressed("ui_cancel") && checkingForCancel && cancelAllowed) CancelRebind();
}

}  // namespace scripts
