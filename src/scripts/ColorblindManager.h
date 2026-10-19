// source: scripts/ColorblindManager.gd (9 lines)
// PORT deviations:
//   (none yet)
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

class Colorblind : public godot::Script {
public:
    static constexpr const char* kClassName = "Colorblind";
    const char* get_class_name() const override { return kClassName; }

    godot::SurfaceMaterial* mat_live = nullptr;  // shared resource (MaterialLibrary)
    godot::SurfaceMaterial* mat_blank = nullptr;  // shared resource (MaterialLibrary)
    godot::SurfaceMaterial* mat_emission = nullptr;  // shared resource (MaterialLibrary)

    void _ready() override;
};

}  // namespace scripts
