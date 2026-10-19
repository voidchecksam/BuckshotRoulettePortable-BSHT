#include "scripts/UserExit.h"

#include "godot/input.h"
#include "godot/input_map.h"
#include "godot/print.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/CursorManager.h"

namespace scripts {

namespace {

// String.trim_suffix / replace(" ", "") / left(n), as KeyRebinding's bind text.
std::string TrimSuffix(std::string s, const std::string& suffix) {
    if (s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
        s.erase(s.size() - suffix.size());
    }
    return s;
}

std::string RemoveSpaces(std::string s) {
    std::string out;
    for (char c : s) {
        if (c != ' ') out += c;
    }
    return out;
}

}  // namespace

// gd: L10-L12
void ExitManager::_ready() {
    get_tree()->spawn(ReadyRoutine(), get_node_owner());
}

seq::Task ExitManager::ReadyRoutine() {
    co_await get_tree()->create_timer(.5);
    exitAllowed = true;
}

// gd: L14-L22
void ExitManager::_process(double delta) {
    const godot::Input& input = godot::Input::get_singleton();
    if (exitAllowed) {
        if (input.is_action_just_pressed("exit game")) ShowUI();
        if (input.is_action_pressed("exit game")) total += delta;
    }
    if (input.is_action_just_released("exit game")) total = 0;
    CheckExit();
}

// The Vita pad as platform/ maps it onto Godot's joypad buttons.
std::string VitaButtonName(int index) {
    switch (index) {
    case godot::joy::A: return "CROSS";
    case godot::joy::B: return "CIRCLE";
    case godot::joy::X: return "SQUARE";
    case godot::joy::Y: return "TRIANGLE";
    case godot::joy::BACK: return "SELECT";
    case godot::joy::START: return "START";
    case 9: return "L";
    case 10: return "R";
    case godot::joy::DPAD_UP: return "UP";
    case godot::joy::DPAD_DOWN: return "DOWN";
    case godot::joy::DPAD_LEFT: return "LEFT";
    case godot::joy::DPAD_RIGHT: return "RIGHT";
    default: return "BUTTON " + std::to_string(index);
    }
}

// gd: L24-L39
void ExitManager::ShowUI() {
    const auto& events = godot::InputMap::get_singleton().action_get_events("exit game");
    if (events.size() < 2) return;   // gd: events[1] would error with fewer
    std::string bind_keyboard = events[0]->as_text();
    std::string bind_controller = events[1]->as_text();
    const std::size_t firstindex = bind_controller.rfind('(');
    bind_keyboard = TrimSuffix(bind_keyboard, "(Physical)");
    bind_keyboard = RemoveSpaces(bind_keyboard);
    // String.left(-1) (no "(") keeps all but the last character.
    bind_controller = firstindex == std::string::npos
                          ? bind_controller.substr(0, bind_controller.empty() ? 0 : bind_controller.size() - 1)
                          : bind_controller.substr(0, firstindex);
    if (vitaPadNames && events[1]->type == godot::InputEventType::JoypadButton) {
        bind_controller = VitaButtonName(static_cast<const godot::InputEventJoypadButton&>(*events[1]).button_index);
    }
    const std::string bind = (cursor && cursor->controller_active) ? bind_controller : bind_keyboard;
    if (label) label->set_text(godot::format(godot::tr("HOLD TO EXIT"), {bind}));
    if (anim) {
        anim->play("RESET");
        anim->play("fade out");
    }
}

// gd: L41-L44
void ExitManager::CheckExit() {
    if (total > 2 && exitAllowed) {
        ExitGame();
        exitAllowed = false;
    }
}

// gd: L46-L48
void ExitManager::ExitGame() {
    godot::print("changing scene to: menu");
    get_tree()->change_scene_to_file("res://scenes/menu.tscn");
}

}  // namespace scripts
