// source: scripts/EndlessMode.gd (25 lines)
// PORT deviations:
//   - `display` is the shared mat_round indicator main.tres (MaterialLibrary),
//     the rounds display's material_override; `displayrounds` is the baker's
//     key for the endless texture (godot::TextureKeyForResource), which the
//     renderer matches to the variant baked for it.
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

class Endless : public godot::Script {
public:
    static constexpr const char* kClassName = "Endless";
    const char* get_class_name() const override { return kClassName; }

    RoundManager* roundManager = nullptr;
    godot::Node3D* smokerdude = nullptr;
    godot::SurfaceMaterial* display = nullptr;  // shared resource (MaterialLibrary)
    std::string displayrounds;  // the baker's texture key (TextureKeyForResource)
    Amounts* amounts = nullptr;

    void SetupEndless();                                     // gd: L9-L25
};

}  // namespace scripts
