// source: scripts/ButtonClass.gd (67 lines)
// PORT deviations: none.
#pragma once

#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>

namespace scripts {

class ButtonClass : public godot::Script {
public:
    static constexpr const char* kClassName = "ButtonClass";
    const char* get_class_name() const override { return kClassName; }

    // v2.2
    // PORT: @export pipe : LobbyManager (multiplayer; null in single-player scenes)
    bool adding_cursor = false;
    godot::Label* t = nullptr;
    std::string orig;

    CursorManager* cursor = nullptr;
    std::string alias;
    bool isActive = false;
    bool isDynamic = false;
    godot::Control* ui = nullptr;
    godot::Control* ui_control = nullptr;
    godot::AudioStreamPlayer* speaker_press = nullptr;
    godot::AudioStreamPlayer* speaker_hover = nullptr;
    godot::Node* rebind = nullptr;
    bool language = false;
    OptionsManager* options = nullptr;
    Rebinding* rebindManager = nullptr;
    bool playing = false;
    bool altsound = false;
    double ui_opacity_inactive = 0.0;
    double ui_opacity_active = 0.0;
    bool resetting = false;
    bool mainActive = true;

    void _ready() override;                     // gd: L26-L34
    void SetFilter(const std::string& alias);   // gd: L36-L41
    void OnHover();                             // gd: L43-L50
    void OnExit();                              // gd: L52-L57
    void OnPress();                             // gd: L60-L67
};

}  // namespace scripts
