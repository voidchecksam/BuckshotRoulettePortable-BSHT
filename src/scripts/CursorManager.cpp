#include "scripts/CursorManager.h"

#include "godot/input.h"

namespace scripts {

// gd: L10-L11
void CursorManager::_ready() {
    SetCursor(false, false);
}

// gd: L13-L21
void CursorManager::SetCursor(bool isVisible, bool playSound) {
    godot::Input& input = godot::Input::get_singleton();
    if (playSound) speaker->play();
    if (isVisible) {
        if (!controller_active) {
            input.set_mouse_mode(godot::Input::MouseMode::Visible);
        } else {
            input.set_mouse_mode(godot::Input::MouseMode::Hidden);
        }
        cursor_visible = true;
    }
    if (!isVisible) {
        input.set_mouse_mode(godot::Input::MouseMode::Hidden);
        cursor_visible = false;
    }
}

// gd: L23-L28
void CursorManager::SetCursorImage(const std::string& alias) {
    godot::Input& input = godot::Input::get_singleton();
    if (alias == "point") {
        input.set_custom_mouse_cursor(cursor_point, 0, {12.0f, 0.0f});
    } else if (alias == "hover") {
        input.set_custom_mouse_cursor(cursor_hover, 0, {9.0f, 0.0f});
    } else if (alias == "invalid") {
        input.set_custom_mouse_cursor(cursor_invalid, 0, {12.0f, 0.0f});
    }
}

}  // namespace scripts
