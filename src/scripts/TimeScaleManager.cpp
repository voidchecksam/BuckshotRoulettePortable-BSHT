#include "scripts/TimeScaleManager.h"

#include "godot/math.h"
#include "godot/scene_tree.h"
#include "scripts/GlobalVariables.h"

namespace scripts {

// gd: L11-L12
void TimeScaleManager::_process(double /*delta*/) {
    LerpTimeScale();
}

// gd: L14-L19
void TimeScaleManager::BeginTimeScaleLerp(double start, double end) {
    moving = false;
    elapsed = 0;
    from = start;
    to = end;
    moving = true;
}

// gd: L21-L27
void TimeScaleManager::LerpTimeScale() {
    if (moving) {
        elapsed += get_process_delta_time();
        double c = elapsed / timeScaleLerpDuration;
        c = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);       // clampf(c, 0.0, 1.0)
        c = godot::ease(c, 0.4);
        const double val = from + (to - from) * c;      // lerpf(from, to, c)
        get_tree()->set_time_scale(val);                // Engine.time_scale
    }
}

// gd: L29-L36
void TimeScaleManager::_unhandled_input(const godot::InputEventRef& event) {
    if (GlobalVariables::mp_debugging) {
        if (event->is_action_pressed("-")) moving = false;
        if (event->is_action_pressed(",")) moving = false;
        if (event->is_action_pressed(".")) moving = false;
    }
}

}  // namespace scripts
