#include "scripts/OptionsManager.h"

#include "game/save_store.h"
#include "godot/audio_server.h"
#include "godot/input_map.h"
#include "godot/math_funcs.h"
#include "godot/print.h"
#include "godot/translation.h"
#include "scripts/ButtonClass.h"
#include "scripts/ChecmarkBranch.h"
#include "scripts/ControllerManager.h"
#include "scripts/GlobalVariables.h"
#include "scripts/KeyRebinding.h"
#include "scripts/MenuManager.h"

#include <cmath>
#include <cstdio>

namespace scripts {

namespace {

// str() of the settings Dictionary as Godot 4.1 writes it (variant.cpp
// stringify: { "key": value, ... }); the keys come sorted from the save map.
std::string SettingsText(const game::SaveMap& data) {
    std::string out = "{ ";
    bool first = true;
    for (const auto& [key, value] : data) {
        if (!first) out += ", ";
        first = false;
        out += "\"" + key + "\": ";
        if (const auto* b = std::get_if<bool>(&value)) out += *b ? "true" : "false";
        else if (const auto* i = std::get_if<int64_t>(&value)) out += std::to_string(*i);
        else if (const auto* d = std::get_if<double>(&value)) out += godot::print_str(*d);
        else if (const auto* t = std::get_if<std::string>(&value)) out += "\"" + *t + "\"";
    }
    return out + " }";
}

}  // namespace
namespace {

const char* const kActions[] = {"ui_accept", "ui_cancel", "ui_left",   "ui_right",        "ui_up",
                                "ui_down",   "reset",     "exit game", "free look toggle"};

void SetAlpha(godot::Control* c, float a) {
    if (!c) return;
    godot::Color m = c->get_modulate();
    m.a = a;
    c->set_modulate(m);
}

}  // namespace

// Math::snapped, then Variant FLOAT's string (rtos, 14 significant digits).
std::string VolumeLabel(double settingVolume) {
    const double step = .01;
    const double snapped = std::floor(settingVolume * 100 / step + 0.5) * step;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.14g", snapped);
    std::string s = buf;
    if (s == "-0") s = "0";
    return s + "%";
}

InputMapSetting OptionsManager::CurrentBindings(int index) {
    InputMapSetting out;
    const godot::InputMap& map = godot::InputMap::get_singleton();
    for (const char* action : kActions) {
        const auto& events = map.action_get_events(action);
        out.emplace_back(action, index < static_cast<int>(events.size()) ? events[static_cast<std::size_t>(index)]
                                                                         : godot::InputEventRef());
    }
    return out;
}

// gd: L55-L67
void OptionsManager::_ready() {
    // gd: L8-L29. The defaults are read from the InputMap when the script
    // instance is made, before any save could change it.
    defaultOption_inputmap_keyboard = CurrentBindings(0);
    defaultOption_inputmap_controller = CurrentBindings(1);
    LoadSettings();
    if (!receivedFile) {
        setting_windowed = defaultOption_windowed;
        setting_controllerEnabled = defaultOption_controllerActive;
        setting_language = defaultOption_language;
        setting_inputmap_keyboard = defaultOption_inputmap_keyboard;
        setting_inputmap_controller = defaultOption_inputmap_controller;
        ApplySettings_window();
        ApplySettings_controller();
        ApplySettings_language();
        ApplySettings_inputmap();
        ApplySettings_greyscaledeath();
    }
}

// gd: L69-L71
void OptionsManager::Printout() {
    godot::print("user current version: ", GlobalVariables::currentVersion);
    godot::print("user current  hotfix: ", GlobalVariables::currentVersion_hotfix);
}

// gd: L73-L102
void OptionsManager::Adjust(const std::string& alias) {
    if (alias == "increase") {
        if (setting_volume != 1) {
            setting_volume = setting_volume + 0.05;
            if (setting_volume > 1) setting_volume = 1;
        }
        UpdateDisplay();
        ApplySettings_volume();
    } else if (alias == "decrease") {
        if (setting_volume != 0) setting_volume = setting_volume - 0.05;
        if (setting_volume < 0) setting_volume = 0;
        UpdateDisplay();
        ApplySettings_volume();
    } else if (alias == "controller enable") {
        setting_controllerEnabled = true;
        ApplySettings_controller();
    } else if (alias == "controller disable") {
        setting_controllerEnabled = false;
        ApplySettings_controller();
    } else if (alias == "windowed") {
        setting_windowed = true;
        ApplySettings_window();
    } else if (alias == "fullscreen") {
        setting_windowed = false;
        ApplySettings_window();
    }
    if (alias != "increase" && alias != "decrease" && menu) menu->ResetButtons();
}

// gd: L104-L107
void OptionsManager::ToggleColorblind() {
    setting_colorblind = !setting_colorblind;
    checkmark_colorblind->UpdateCheckmark(setting_colorblind);
    ApplySettings_colorblind();
}

// gd: L109-L112
void OptionsManager::ToggleGreyscaleDeath() {
    setting_greyscale_death = !setting_greyscale_death;
    checkmark_greyscale->UpdateCheckmark(setting_greyscale_death);
    ApplySettings_greyscaledeath();
}

// gd: L114-L117
void OptionsManager::AdjustLanguage(const std::string& alias) {
    setting_language = alias;
    ApplySettings_language();
    if (menu) menu->ResetButtons();
}

// gd: L124-L126
void OptionsManager::AdjustSettings_music() {
    setting_music_enabled = !setting_music_enabled;
    ApplySettings_music();
}

// gd: L129-L136
void OptionsManager::ApplySettings_music() {
    if (setting_music_enabled) anim_vinyl->set_speed_scale(1);
    else anim_vinyl->set_speed_scale(0);
    godot::AudioServer& server = godot::AudioServer::get_singleton();
    server.set_bus_mute(1, !setting_music_enabled);   // music
    server.set_bus_mute(2, !setting_music_enabled);   // music secondary
    server.set_bus_mute(4, !setting_music_enabled);   // music resolve
    GlobalVariables::music_enabled = setting_music_enabled;
}

// gd: L119-L122
void OptionsManager::ApplySettings_volume() {
    godot::AudioServer::get_singleton().set_bus_volume_db(0, static_cast<float>(godot::linear_to_db(setting_volume)));
    UpdateDisplay();
}

// gd: L138-L139
void OptionsManager::UpdateDisplay() {
    if (ui_volume) ui_volume->set_text(VolumeLabel(setting_volume));
}

// gd: L141-L154. PORT: no DisplayServer window mode (see header).
void OptionsManager::ApplySettings_window() {
    if (!setting_windowed) {
        SetAlpha(ui_fullscreen, 1);
        SetAlpha(ui_windowed, .5f);
    } else {
        SetAlpha(ui_fullscreen, .5f);
        SetAlpha(ui_windowed, 1);
    }
    if (button_fullscreen) button_fullscreen->mainActive = true;
    if (button_windowed) button_windowed->mainActive = true;
}

// gd: L156-L166
void OptionsManager::ApplySettings_controller() {
    if (setting_controllerEnabled) {
        if (controller) controller->SetMainControllerState(true);
        SetAlpha(ui_deviceController, 1);
        SetAlpha(ui_deviceMouse, .5f);
        GlobalVariables::controllerEnabled = true;
    } else {
        if (controller) controller->SetMainControllerState(false);
        SetAlpha(ui_deviceController, .5f);
        SetAlpha(ui_deviceMouse, 1);
        GlobalVariables::controllerEnabled = false;
    }
}

// gd: L168-L190
void OptionsManager::ParseInputMapDictionary() {
    setting_inputmap_keyboard = CurrentBindings(0);
    setting_inputmap_controller = CurrentBindings(1);
}

// gd: L192-L196
void OptionsManager::ResetControls() {
    setting_inputmap_keyboard = defaultOption_inputmap_keyboard;
    setting_inputmap_controller = defaultOption_inputmap_controller;
    ApplySettings_inputmap();
    if (keyrebinding) keyrebinding->UpdateBindList();
}

// gd: L200-L211
void OptionsManager::ApplySettings_inputmap() {
    godot::InputMap& map = godot::InputMap::get_singleton();
    for (const auto& [key, value] : setting_inputmap_keyboard) {
        map.action_erase_events(key);
        if (value) map.action_add_event(key, value);
    }
    for (const auto& [key, value] : setting_inputmap_controller) {
        if (value) map.action_add_event(key, value);
    }
    if (keyrebinding) keyrebinding->UpdateBindList();
    setting = false;
}

// gd: L213-L214
void OptionsManager::ApplySettings_language() {
    // PORT: the original's TURKISH button sends "TU" but its table is "TR", so
    // Godot shows raw keys there (JankPreventer.gd:15 checks "TR": TR was
    // meant), and every language the menu offers should draw. The saved value
    // stays "TU".
    godot::set_locale(setting_language == "TU" ? std::string("TR") : setting_language);
}

// gd: L216-L217
void OptionsManager::ApplySettings_colorblind() { GlobalVariables::colorblind = setting_colorblind; }

// gd: L219-L221
void OptionsManager::ApplySettings_greyscaledeath() {
    GlobalVariables::greyscale_death = setting_greyscale_death;
    checkmark_greyscale->UpdateCheckmark(GlobalVariables::greyscale_death);
}

// gd: L223-L240
void OptionsManager::SaveSettings() {
    if (!store) return;
    game::SaveMap data = {
        {"setting_volume", setting_volume},
        {"setting_windowed", setting_windowed},
        {"setting_language", setting_language},
        {"setting_controllerEnabled", setting_controllerEnabled},
        {"setting_colorblind", setting_colorblind},
        {"setting_music_enabled", setting_music_enabled},
        {"setting_greyscale_death", setting_greyscale_death},
    };
    for (const auto& [key, value] : setting_inputmap_keyboard) {
        if (value) data["setting_inputmap_keyboard/" + key] = godot::event_to_str(*value);
    }
    for (const auto& [key, value] : setting_inputmap_controller) {
        if (value) data["setting_inputmap_controller/" + key] = godot::event_to_str(*value);
    }
    godot::print("attempting to save settings");
    store->Save(savePath, data);
    godot::print("file closed");
}

bool OptionsManager::SavedMusicEnabled(const game::SaveStore* store) {
    game::SaveMap data;
    if (!store || !store->Load(savePath, data)) return true;
    const auto it = data.find("setting_music_enabled");
    const bool* on = it == data.end() ? nullptr : std::get_if<bool>(&it->second);
    return on ? *on : true;
}

// gd: L243-L278
void OptionsManager::LoadSettings() {
    if (!store) return;
    game::SaveMap data;
    if (!store->Load(savePath, data)) {   // gd: FileAccess.file_exists
        godot::print("user does not have settings file");
        return;
    }
    godot::print("settings file found, attempting to load settings: ");
    const auto num = [&](const char* k, double d) {
        const auto it = data.find(k);
        if (it == data.end()) return d;
        if (const double* v = std::get_if<double>(&it->second)) return *v;
        if (const int64_t* v = std::get_if<int64_t>(&it->second)) return static_cast<double>(*v);
        return d;
    };
    const auto flag = [&](const char* k, bool d) {
        const auto it = data.find(k);
        const bool* v = it == data.end() ? nullptr : std::get_if<bool>(&it->second);
        return v ? *v : d;
    };
    setting_volume = num("setting_volume", setting_volume);
    setting_windowed = flag("setting_windowed", setting_windowed);
    if (const auto it = data.find("setting_language"); it != data.end()) {
        if (const std::string* v = std::get_if<std::string>(&it->second)) setting_language = *v;
    }
    setting_controllerEnabled = flag("setting_controllerEnabled", setting_controllerEnabled);
    const auto events = [&](const char* prefix, int index) {
        InputMapSetting out = CurrentBindings(index);
        for (auto& [key, value] : out) {
            const auto it = data.find(std::string(prefix) + key);
            const std::string* text = it == data.end() ? nullptr : std::get_if<std::string>(&it->second);
            if (!text) continue;
            if (godot::InputEventRef e = godot::str_to_event(*text)) value = e;
        }
        return out;
    };
    setting_inputmap_keyboard = events("setting_inputmap_keyboard/", 0);
    setting_inputmap_controller = events("setting_inputmap_controller/", 1);
    if (data.count("setting_colorblind")) {
        setting_colorblind = flag("setting_colorblind", setting_colorblind);
        if (checkmark_colorblind) checkmark_colorblind->UpdateCheckmark(setting_colorblind);
    }
    if (data.count("setting_music_enabled")) setting_music_enabled = flag("setting_music_enabled", setting_music_enabled);
    if (data.count("setting_greyscale_death")) {
        setting_greyscale_death = flag("setting_greyscale_death", setting_greyscale_death);
        if (checkmark_greyscale) checkmark_greyscale->UpdateCheckmark(setting_greyscale_death);
    }
    godot::print("---------------------------------");
    godot::print("user settings: ", SettingsText(data));
    godot::print("---------------------------------");
    godot::print("settings file closed");
    setting = true;
    ApplySettings_music();
    ApplySettings_volume();
    ApplySettings_window();
    ApplySettings_language();
    ApplySettings_controller();
    ApplySettings_inputmap();
    ApplySettings_colorblind();
    ApplySettings_greyscaledeath();
    receivedFile = true;
}

}  // namespace scripts
