// source: scripts/ShellBranch.gd (19 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

namespace scripts {

class ShellClass : public godot::Script {
public:
    static constexpr const char* kClassName = "ShellClass";
    const char* get_class_name() const override { return kClassName; }

    bool isLive = false;
    godot::SurfaceMaterial* mat_live = nullptr;   // shared resource (MaterialLibrary)
    godot::SurfaceMaterial* mat_blank = nullptr;  // shared resource (MaterialLibrary)
    godot::MeshInstance3D* mesh = nullptr;

    void _ready() override;                           // gd: L8-L9
    bool has_process() const override { return true; }
    void _process(double delta) override;             // gd: L11-L12
    void ApplyStatus();                               // gd: L14-L19
};

}  // namespace scripts
