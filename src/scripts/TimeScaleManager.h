// source: scripts/TimeScaleManager.gd (36 lines)
// PORT deviations:
//   - `from` / `to` are untyped Variants in the .gd, always assigned floats by
//     the only caller (ShotgunShooting.MainSlowDownRoutine); double here.
#pragma once

#include "godot/script.h"

namespace scripts {

// Lerps Engine.time_scale. A time_scale set during _process applies to the
// next frame (Godot 4.1.4, measured), so the slow motion on a killing blow
// starts one frame after BeginTimeScaleLerp.
class TimeScaleManager : public godot::Script {
public:
    static constexpr const char* kClassName = "TimeScaleManager";
    const char* get_class_name() const override { return kClassName; }

    double pitchShiftDuration = 0.0;
    double timeScaleLerpDuration = 0.0;

    bool moving = false;
    double elapsed = 0.0;
    double from = 0.0;
    double to = 0.0;

    bool has_process() const override { return true; }
    void _process(double delta) override;                 // gd: L11-L12
    void BeginTimeScaleLerp(double start, double end);    // gd: L14-L19
    void LerpTimeScale();                                 // gd: L21-L27
    bool has_unhandled_input() const override { return true; }
    void _unhandled_input(const godot::InputEventRef& event) override;   // gd: L29-L36
};

}  // namespace scripts
