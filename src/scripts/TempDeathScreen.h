// source: scripts/TempDeathScreen.gd (23 lines)
// PORT deviations:
//   (none; the commented-out _unhandled_input is not translated, as in the .gd)
#pragma once

#include "godot/control.h"
#include "godot/script.h"
#include "scripts/forward.h"

namespace scripts {

class TempDeathScreen : public godot::Script {
public:
    static constexpr const char* kClassName = "TempDeathScreen";
    const char* get_class_name() const override { return kClassName; }

    bool isDeathScreen = false;
    SaveFileManager* savefile = nullptr;
    godot::ColorRect* viewblocker = nullptr;
    godot::AudioStreamPlayer* speaker = nullptr;
    bool allowed = true;
    bool fs = false;

    void _ready() override;   // gd: L10-L13
};

}  // namespace scripts
