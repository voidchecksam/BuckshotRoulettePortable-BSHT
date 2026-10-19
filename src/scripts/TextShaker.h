// source: scripts/TextShaker.gd (24 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

namespace scripts {

class TextShaker : public godot::Script {
public:
    static constexpr const char* kClassName = "TextShaker";
    const char* get_class_name() const override { return kClassName; }

    double delay = 0.0;
    double offset1 = 0.0;
    double offset2 = 0.0;
    godot::Control* text = nullptr;

    bool looping = false;
    godot::Vector2 origpos;

    void _ready() override;   // gd: L10-L13
    seq::Task Shake();        // gd: L15-L24
};

}  // namespace scripts
