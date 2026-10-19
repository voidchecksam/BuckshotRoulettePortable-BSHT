// source: scripts/BlinkerBranch.gd (23 lines)
// PORT deviations:
//   - mat_1 / mat_2 are the exported StandardMaterial3D's albedo, emission and
//     transparency (godot::SurfaceMaterial), which is what the renderer draws.
#pragma once

#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

namespace scripts {

class BlinkerBranch : public godot::Script {
public:
    static constexpr const char* kClassName = "BlinkerBranch";
    const char* get_class_name() const override { return kClassName; }

    godot::SurfaceMaterial mat_1;
    godot::SurfaceMaterial mat_2;
    double delay_range1 = 0.0;
    double delay_range2 = 0.0;

    godot::MeshInstance3D* parent = nullptr;
    bool looping = false;

    void _ready() override;   // gd: L10-L14
    seq::Task Loop();         // gd: L16-L23
};

}  // namespace scripts
