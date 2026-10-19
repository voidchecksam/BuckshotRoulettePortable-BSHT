// source: scripts/GlobalVariables.gd
// PORT deviations:
//   - An autoload in project.godot. The port has no autoloads, so its fields
//     are static: every script reads the same value, as with the autoload's
//     single instance. GameApp runs _ready once at start.
//   - using_steam is true: v2.2 gates the restroom CRT (the local leaderboard)
//     and the achievements on it, which the port keeps.
//   - The multiplayer fields no single-player script reads are left out
//     (lobby, packets, match customisation).
#pragma once

#include "godot/control.h"
#include "godot/value_types.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>

namespace scripts {

class GlobalVariables : public godot::Script {
public:
    static constexpr const char* kClassName = "GlobalVariables";
    const char* get_class_name() const override { return kClassName; }

    static inline std::string currentVersion_nr = "v2.2.0";
    static inline int currentVersion_hotfix = 6;
    static inline bool using_steam = true;

    static inline std::string currentVersion;
    static inline std::string versuffix_steam = " (STEAM)";
    static inline std::string versuffix_itch = " (ITCH.IO)";

    static inline std::string discord_link = "https://discord.gg/UdjMNaKkQe";

    static inline bool using_gl = false;
    static inline bool controllerEnabled = false;
    static inline bool music_enabled = true;
    static inline godot::Control* current_button_hovered_over = nullptr;

    static inline bool colorblind = false;
    static inline godot::Color colorblind_color{1, 1, 0, 1};
    static inline bool greyscale_death = false;
    static inline bool looping_input_main = false;
    static inline bool looping_input_secondary = false;
    static inline bool cursor_state_after_toggle = false;

    static inline godot::Color default_color_live{1, 0.28f, 0.29f, 1};
    static inline godot::Color default_color_blank{0.29f, 0.5f, 1, 1};
    static inline godot::Color colorblind_color_live{1, 1, 1, 1};
    static inline godot::Color colorblind_color_blank{0.34f, 0.34f, 0.34f, 1};

    static inline bool mp_debugging = false;
    static inline bool mp_debug_keys_enabled = false;
    static inline std::string message_to_forward;
    static inline int debug_round_index_to_end_game_at = 0;
    static inline bool skipping_intro = false;
    static inline long long lobby_id_found_in_command_line = 0;
    static inline bool running_short_intro_in_lobby_scene = false;
    static inline bool command_line_checked = false;
    static inline std::string version_to_check;
    static inline bool returning_to_main_menu_on_popup_close = false;

    void _ready() override;
    // PORT: the autoload is not in the tree, so this never receives input; it
    // acts only while mp_debugging, which single-player never sets.
    void _unhandled_input(const godot::InputEventRef& event) override;
    void SwapLanguage(bool dir);

private:
    int index = 0;
};

}  // namespace scripts
