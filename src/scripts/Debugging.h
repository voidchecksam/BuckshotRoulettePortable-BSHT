// source: scripts/Debugging.gd (13 lines); class_name Debug
#pragma once

#include "godot/script.h"
#include "scripts/forward.h"

namespace scripts {

class Debug : public godot::Script {
public:
    static constexpr const char* kClassName = "Debug";
    const char* get_class_name() const override { return kClassName; }

    bool debugging = false;
    TimeScaleManager* timescale = nullptr;

    bool has_unhandled_input() const override { return true; }
    void _unhandled_input(const godot::InputEventRef& event) override;   // gd: L6-L13
};

}  // namespace scripts
