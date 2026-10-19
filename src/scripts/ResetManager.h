// source: scripts/ResetManager.gd (30 lines)
// PORT deviations:
//   padResets: the original's "reset" (R, pad button 3) clears the run and
//     restarts it; on the Vita pad button 3 is Triangle, under the thumb, so
//     the pad's reset is off there. R still resets.
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

class ResetManager : public godot::Script {
public:
    static constexpr const char* kClassName = "ResetManager";
    const char* get_class_name() const override { return kClassName; }

    SaveFileManager* save = nullptr;

    bool fs = false;
    bool reset_pressed = false;
    double reset_timer = 0.0;
    bool padResets = true;   // PORT: false on the Vita (see above)

    bool has_unhandled_input() const override { return true; }
    void _unhandled_input(const godot::InputEventRef& event) override;   // gd: L8-L14
    bool has_process() const override { return true; }
    void _process(double delta) override;                                // gd: L16-L24
    void Reset();                                                        // gd: L26-L30
};

}  // namespace scripts
