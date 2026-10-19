#include "scripts/GlobalVariables.h"

#include "godot/print.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"

#include <array>

namespace scripts {

namespace {

const std::array<const char*, 16> kLanguages = {"EN", "EE", "RU", "ES LATAM", "ES", "FR", "IT", "JA",
                                                "KO", "PL", "PT", "DE", "TR", "UA", "ZHS", "ZHT"};

}  // namespace

// gd: L273-L283
void GlobalVariables::_ready() {
    if (using_steam) currentVersion = currentVersion_nr + versuffix_steam;
    else currentVersion = currentVersion_nr + versuffix_itch;
    debug_round_index_to_end_game_at = 2;
    // PORT: original_volume_linear_interaction / _music (buses 3 and 1) are
    // read only by multiplayer; the port's AudioServer has bus 0 alone.
    version_to_check = currentVersion_nr + "." + std::to_string(currentVersion_hotfix);
    godot::print("running full version name: ", version_to_check);
    if (mp_debugging) {
        godot::set_locale("EN");
        // PORT: active_match_customization_dictionary is multiplayer's.
    }
}

// gd: L285-L298
void GlobalVariables::_unhandled_input(const godot::InputEventRef& event) {
    if (!(mp_debugging || mp_debug_keys_enabled)) return;
    if (event->is_action_pressed("debug_q")) SwapLanguage(false);
    if (event->is_action_pressed("debug_e")) SwapLanguage(true);
    godot::SceneTree* tree = get_tree();
    if (!tree) return;
    if (event->is_action_pressed("-")) tree->set_time_scale(.05);
    if (event->is_action_pressed(",")) tree->set_time_scale(1);
    if (event->is_action_pressed(".")) tree->set_time_scale(10);
    if (event->is_action_pressed("end")) tree->set_time_scale(0);
}

// gd: L302-L314
void GlobalVariables::SwapLanguage(bool dir) {
    const int last = static_cast<int>(kLanguages.size()) - 1;
    if (dir) {
        if (index == last) index = 0;
        else index += 1;
    } else {
        if (index == 0) index = last;
        else index -= 1;
    }
    godot::set_locale(kLanguages[static_cast<std::size_t>(index)]);
    godot::print("setting locale to: ", kLanguages[static_cast<std::size_t>(index)]);
}

}  // namespace scripts
