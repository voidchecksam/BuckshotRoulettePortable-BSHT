// The one way anything skips the restroom intro: diagnostics, parity scenarios
// and test fixtures. tools/godot/dump_state.gd does the same in Godot: it frees
// the intro manager and hides its blackout. A player's boot never calls it.
// Header-only, like item_scenes.h: not a translated script, so it carries no
// .cpp for scripts_mirror_gdscript to pair with a .gd.
#pragma once

#include "godot/control.h"
#include "godot/scene_tree.h"

namespace scripts {

// PORT: the original runs the intro from main.tscn's first frame; the port's
// diagnostics start at the table instead. Idempotent. The intro manager's
// pending awaits die with it (SceneTree cancels a freed node's sequences).
inline void SkipIntro(godot::SceneTree& tree) {
    if (godot::Node* intro = tree.get_node("standalone managers/intro manager")) intro->queue_free();
    if (auto* blocker = tree.get_node_as<godot::Control>("Camera/dialogue UI/viewblocker parent/viewblcoker")) {
        blocker->set_visible(false);
    }
}

}  // namespace scripts
