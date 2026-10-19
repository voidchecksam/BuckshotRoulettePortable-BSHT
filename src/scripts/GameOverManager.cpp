#include "scripts/GameOverManager.h"

#include "godot/scene_tree.h"

namespace scripts {

// gd: L3-L4
void GameOverManager::PlayerWon() {
    get_tree()->change_scene_to_file("res://scenes/win.tscn");
}

}  // namespace scripts
