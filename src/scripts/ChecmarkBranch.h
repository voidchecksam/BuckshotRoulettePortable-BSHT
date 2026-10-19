// source: scripts/ChecmarkBranch.gd (9 lines)
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

class Checkmark : public godot::Script {
public:
    static constexpr const char* kClassName = "Checkmark";
    const char* get_class_name() const override { return kClassName; }

    godot::TextureRect* ui = nullptr;
    std::string checkmark_active;  // res:// path, the UI layout's texture key
    std::string checkmark_ianctive;  // res:// path, the UI layout's texture key

    void UpdateCheckmark(bool state);
};

}  // namespace scripts
