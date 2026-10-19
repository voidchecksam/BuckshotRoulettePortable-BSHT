#include "scripts/TempDeathScreen.h"

#include "godot/print.h"
#include "godot/scene_tree.h"

namespace scripts {

// gd: L10-L13
void TempDeathScreen::_ready() {
    if (isDeathScreen) {
        godot::print("changing scene to: main");
        get_tree()->change_scene_to_file("res://scenes/main.tscn");
    }
}

}  // namespace scripts
