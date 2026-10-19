#include "scripts/ResetManager.h"

#include "godot/print.h"
#include "godot/scene_tree.h"
#include "scripts/SaveFileManager.h"

namespace scripts {

// gd: L8-L14
void ResetManager::_unhandled_input(const godot::InputEventRef& event) {
    // PORT: on the Vita a pad button never resets the run (padResets).
    if (!padResets && event->type == godot::InputEventType::JoypadButton) return;
    if (event->is_action_pressed("reset") && !fs) {
        reset_timer = 0;
        reset_pressed = true;
    }
    if (event->is_action_released("reset")) {
        reset_timer = 0;
        reset_pressed = false;
    }
}

// gd: L16-L24
void ResetManager::_process(double delta) {
    if (reset_pressed) {
        reset_timer += delta;
        if (reset_timer >= 0.8) {
            reset_pressed = false;
            Reset();
            reset_timer = 0.0;
        }
    } else {
        reset_timer = 0.0;
    }
}

// gd: L26-L30
void ResetManager::Reset() {
    if (save) save->ClearSave();
    godot::print("changing scene to: death");
    get_tree()->change_scene_to_file("res://scenes/death.tscn");
    fs = true;
}

}  // namespace scripts
