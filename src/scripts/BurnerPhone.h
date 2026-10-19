// source: scripts/BurnerPhone.gd (38 lines)
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

class BurnerPhone : public godot::Script {
public:
    static constexpr const char* kClassName = "BurnerPhone";
    const char* get_class_name() const override { return kClassName; }

    ShellSpawner* sh = nullptr;
    Dialogue* dia = nullptr;

    // Animation METHOD track: "player use burner phone" calls SendDialogue.
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    seq::Task SendDialogue();   // gd: L6-L35
};

}  // namespace scripts
