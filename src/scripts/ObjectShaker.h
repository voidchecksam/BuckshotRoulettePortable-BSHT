// source: scripts/ObjectShaker.gd (28 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

namespace scripts {

class ObjectShaker : public godot::Script {
public:
    static constexpr const char* kClassName = "ObjectShaker";
    const char* get_class_name() const override { return kClassName; }

    godot::Node3D* obj = nullptr;
    double delay = 0.0;
    double offset_1 = 0.0;
    double offset_2 = 0.0;

    godot::Vector3 originalPos;
    bool shaking = false;

    void StartShaking();        // gd: L11-L14
    void StopShaking();         // gd: L16-L18
    seq::Task ShakeRoutine();   // gd: L20-L28
};

}  // namespace scripts
