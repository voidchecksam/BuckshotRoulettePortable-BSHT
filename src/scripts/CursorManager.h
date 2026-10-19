// source: scripts/CursorManager.gd (28 lines)
// PORT deviations:
//   - Input.set_mouse_mode / set_custom_mouse_cursor record their state on
//     godot::Input; the desktop platform draws the original cursor images and
//     the Vita (touch) draws none. The cursor resources are their file names.
#pragma once

#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>

namespace scripts {

class CursorManager : public godot::Script {
public:
    static constexpr const char* kClassName = "CursorManager";
    const char* get_class_name() const override { return kClassName; }

    godot::AudioStreamPlayer* speaker = nullptr;
    std::string cursor_point;
    std::string cursor_hover;
    std::string cursor_invalid;
    bool cursor_visible = false;
    bool controller_active = false;

    void _ready() override;                          // gd: L10-L11
    void SetCursor(bool isVisible, bool playSound);  // gd: L13-L21
    void SetCursorImage(const std::string& alias);   // gd: L23-L28
};

}  // namespace scripts
