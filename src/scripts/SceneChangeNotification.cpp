#include "scripts/SceneChangeNotification.h"

#include "godot/print.h"
#include "godot/scene_tree.h"

namespace scripts {

// gd: L3-L4
void SceneChangeNotification::_ready() {
    godot::print("user entered scene: ", get_tree()->get_root()->get_name());
}

}  // namespace scripts
