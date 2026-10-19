// source: scripts/LocalizationManager.gd (1 lines)
// PORT deviations:
//   (none: the .gd declares the class and nothing else)
#pragma once

#include "godot/script.h"
#include "scripts/forward.h"

namespace scripts {

class LocalizationManager : public godot::Script {
public:
    static constexpr const char* kClassName = "LocalizationManager";
    const char* get_class_name() const override { return kClassName; }
};

}  // namespace scripts
