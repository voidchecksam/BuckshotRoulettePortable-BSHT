// source: scripts/CrtIcon.gd (22 lines)
// PORT deviations:
//   - texture_active / texture_inactive are the baker's texture keys
//     (godot::TextureKeyForResource); the renderer draws the variant baked for
//     the key the override holds.
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

class CrtIcon : public godot::Script {
public:
    static constexpr const char* kClassName = "CrtIcon";
    const char* get_class_name() const override { return kClassName; }

    std::string texture_active;  // the baker's texture key (TextureKeyForResource)
    std::string texture_inactive;  // the baker's texture key (TextureKeyForResource)
    int activeIndex = 0;

    godot::GeometryInstance3D* instance = nullptr;

    void _ready() override;                          // gd: L9-L11
    void CheckState(int currentIndex);               // gd: L14-L16
    void SetState(bool act);                         // gd: L18-L22
};

}  // namespace scripts
