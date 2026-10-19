// Godot 4.1.4's InputMap (core/input/input_map.cpp): actions and the events
// bound to them. Loaded from assets/extracted/input_map.json, which
// tools/godot/export_input_map.gd writes from project.godot [input]: the
// original's own bindings, which override the engine defaults.
#pragma once

#include "godot/input_event.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace godot {

class InputMap {
public:
    static constexpr int ALL_DEVICES = -1;   // input_map.cpp:41

    static InputMap& get_singleton();

    void clear() { actions_.clear(); }
    // False when the file is missing or malformed; the map is left empty.
    bool load(const char* json_path);

    void add_action(const std::string& action);
    void action_add_event(const std::string& action, InputEventRef event);
    bool has_action(std::string_view action) const { return find(action) != nullptr; }

    // input_map.cpp event_get_action_status. An unknown action returns false
    // (Godot also prints an error, as the port does to stderr).
    bool event_get_action_status(const InputEvent& event, std::string_view action, bool exact,
                                 bool* r_pressed) const;

    std::vector<std::string> get_actions() const;
    // InputMap.action_get_events: empty for an unknown action.
    const std::vector<InputEventRef>& action_get_events(std::string_view action) const;
    void action_erase_events(std::string_view action);

private:
    struct Action {
        std::string name;
        std::vector<InputEventRef> events;
    };
    const Action* find(std::string_view name) const;
    Action* find(std::string_view name);

    std::vector<Action> actions_;
};

}  // namespace godot
