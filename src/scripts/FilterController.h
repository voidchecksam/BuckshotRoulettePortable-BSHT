// source: scripts/FilterController.gd (38 lines)
// PORT deviations:
//   - the mixer has no low-pass bus effect, so `var effect_lowPass`
//     (AudioServer.get_bus_effect(1, 0)) is modelled as the cutoff value it
//     would hold: effect_lowPass_cutoff_hz. Every function is translated and
//     drives that value exactly as the .gd drives the real effect; only the
//     audible result is missing, and it arrives with the bus effect.
#pragma once

#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

namespace scripts {

class FilterController : public godot::Script {
public:
    static constexpr const char* kClassName = "FilterController";
    const char* get_class_name() const override { return kClassName; }

    double panDuration = 0.0;
    double lowPassDefaultValue = 0.0;
    double lowPassMaxValue = 0.0;

    double effect_lowPass_cutoff_hz = 0.0;
    bool moving = false;
    double elapsed = 0;
    double begin = 0.0;
    double dest = 0.0;

    bool has_process() const override { return true; }
    void _ready() override;                          // gd: L13-L15
    void _process(double delta) override;            // gd: L17-L18
    void BeginSnap(double valueTo);                  // gd: L20-L23
    void BeginPan(double valueFrom, double valueTo); // gd: L25-L30
    void PanLowPass_HighLow();                       // gd: L32-L38
};

}  // namespace scripts
