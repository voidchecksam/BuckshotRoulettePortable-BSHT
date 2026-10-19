// source: scripts/ShellExamine.gd (25 lines)
// PORT deviations:
//   - SetupShell's two Color locals are never read in the .gd; not declared.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class ShellExamine : public godot::Script {
public:
    static constexpr const char* kClassName = "ShellExamine";
    const char* get_class_name() const override { return kClassName; }

    ShellSpawner* shellSpawner = nullptr;
    godot::Node3D* shellParent = nullptr;
    godot::MeshInstance3D* mesh = nullptr;
    godot::SurfaceMaterial mat;
    godot::SurfaceMaterial* mat_blank = nullptr;  // shared resource (MaterialLibrary)
    godot::SurfaceMaterial* mat_live = nullptr;  // shared resource (MaterialLibrary)

    // Animation METHOD tracks: "player use magnifier" calls SetupShell, then
    // RevertShell (trap 4).
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    void _ready() override;   // gd: L10-L11
    void SetupShell();        // gd: L13-L22
    void RevertShell();       // gd: L24-L25
};

}  // namespace scripts
