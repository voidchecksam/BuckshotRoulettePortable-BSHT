#include "scripts/Debugging.h"

#include "godot/scene_tree.h"
#include "scripts/GlobalVariables.h"
#include "scripts/TimeScaleManager.h"

namespace scripts {

// gd: L6-L13
void Debug::_unhandled_input(const godot::InputEventRef& event) {
    if (GlobalVariables::mp_debugging) {
        if (event->is_action_pressed(",")) {
            get_tree()->set_time_scale(1);
            timescale->moving = false;
        }
        if (event->is_action_pressed(".")) {
            get_tree()->set_time_scale(10);
            timescale->moving = false;
        }
    }
}

}  // namespace scripts
