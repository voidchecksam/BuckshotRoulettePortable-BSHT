#include "scripts/FilterController.h"

#include "godot/math.h"
#include "godot/script.h"

namespace scripts {

// gd: L13-L15
void FilterController::_ready() {
    // PORT: effect_lowPass = AudioServer.get_bus_effect(1, 0). The mixer has no
    // bus effects: the cutoff lives here, and the music router applies it to
    // the streamed track.
    effect_lowPass_cutoff_hz = lowPassDefaultValue;
}

// gd: L17-L18
void FilterController::_process(double /*delta*/) {
    PanLowPass_HighLow();
}

// gd: L20-L23
void FilterController::BeginSnap(double valueTo) {
    moving = false;
    elapsed = 0;
    effect_lowPass_cutoff_hz = valueTo;
}

// gd: L25-L30
void FilterController::BeginPan(double valueFrom, double valueTo) {
    moving = false;
    elapsed = 0;
    begin = valueFrom;
    dest = valueTo;
    moving = true;
}

// gd: L32-L38
void FilterController::PanLowPass_HighLow() {
    if (moving) {
        elapsed += get_process_delta_time();
        double c = elapsed / panDuration;
        c = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);        // clampf(c, 0.0, 1.0)
        c = godot::ease(c, 0.4);
        effect_lowPass_cutoff_hz = begin + (dest - begin) * c;   // lerpf
    }
}

}  // namespace scripts
