// source: scripts/OptionsManager.gd
// PORT deviations:
//   - The save (user://buckshotroulette_options_12.shell) goes through
//     game::SaveStore as "options_12", set by the host like SaveFileManager's.
//     The two input-map dictionaries are flattened into keys
//     "setting_inputmap_keyboard/<action>" and ".../controller/<action>", and
//     an event is saved with godot::event_to_str (var_to_str's port).
//   - setting_inputmap_* hold events, not the var_to_str strings
//     ParseInputMapDictionary makes in GDScript; ApplySettings_inputmap
//     therefore never needs str_to_var (`setting` is kept, and cleared, as in
//     the .gd). Dictionaries keep the .gd's insertion order.
//   - DisplayServer.window_set_mode: the Vita has one display mode and the
//     desktop window is the port's; only the two UI alphas change.
//   - AudioServer.set_bus_volume_db(0, ...) sets godot::AudioServer, which the
//     host applies as the mixer's master gain.
//   - GlobalVariables is an autoload; the port holds its fields statically.
//   - AudioServer.set_bus_mute goes to godot::AudioServer, which silences
//     a player on a muted bus (OutputGain); get_property_list is a no-op.
#pragma once

#include "godot/control.h"
#include "godot/input_event.h"
#include "godot/node.h"
#include "godot/animation_player.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>
#include <utility>
#include <vector>

namespace game {
class SaveStore;
}

namespace scripts {

using InputMapSetting = std::vector<std::pair<std::string, godot::InputEventRef>>;

class OptionsManager : public godot::Script {
public:
    static constexpr const char* kClassName = "OptionsManager";
    const char* get_class_name() const override { return kClassName; }

    // v2.2
    Checkmark* checkmark_colorblind = nullptr;
    Checkmark* checkmark_greyscale = nullptr;
    godot::AnimationPlayer* anim_vinyl = nullptr;

    static constexpr const char* savePath = "options_12";
    // The saved music setting, read before OptionsManager exists (the loading
    // screen starts the menu track). True with no store or nothing saved.
    static bool SavedMusicEnabled(const game::SaveStore* store);

    ControllerManager* controller = nullptr;
    std::string defaultOption_language = "EN";
    bool defaultOption_windowed = false;
    bool defaultOption_controllerActive = false;
    InputMapSetting defaultOption_inputmap_keyboard;     // gd: L8-L18
    InputMapSetting defaultOption_inputmap_controller;   // gd: L19-L29
    godot::Control* ui_windowed = nullptr;
    godot::Control* ui_fullscreen = nullptr;
    godot::Control* ui_deviceController = nullptr;
    godot::Control* ui_deviceMouse = nullptr;
    ButtonClass* button_windowed = nullptr;
    ButtonClass* button_fullscreen = nullptr;
    MenuManager* menu = nullptr;
    godot::Label* ui_volume = nullptr;
    Rebinding* keyrebinding = nullptr;
    game::SaveStore* store = nullptr;

    InputMapSetting setting_inputmap_keyboard;
    InputMapSetting setting_inputmap_controller;
    double setting_volume = 1;
    bool setting_windowed = false;
    std::string setting_language = "EN";
    bool setting_controllerEnabled = false;
    bool setting_colorblind = false;
    bool setting_greyscale_death = true;
    bool setting_music_enabled = true;
    bool setting = false;
    bool receivedFile = false;

    void _ready() override;
    void Printout();
    void Adjust(const std::string& alias);
    void ToggleColorblind();
    void ToggleGreyscaleDeath();
    void AdjustLanguage(const std::string& alias);
    void AdjustSettings_music();
    void ApplySettings_music();
    void ApplySettings_volume();
    void UpdateDisplay();
    void ApplySettings_window();
    void ApplySettings_controller();
    void ParseInputMapDictionary();
    void ResetControls();
    void ApplySettings_inputmap();
    void ApplySettings_language();
    void ApplySettings_colorblind();
    void ApplySettings_greyscaledeath();
    void SaveSettings();
    void LoadSettings();

    // The nine actions the .gd lists, in its order; `index` 0 keyboard, 1 pad.
    static InputMapSetting CurrentBindings(int index);
};

// str(snapped(v * 100, .01)) + "%", the volume label (gd: L139), with
// Godot's float-to-string (whole numbers print without ".0").
std::string VolumeLabel(double settingVolume);

}  // namespace scripts
