// source: scripts/ShellDropManager.gd (9 lines)
// PORT deviations:
//   - `var speaker : AudioStreamPlayer2D` is godot::AudioStreamPlayer (the port
//     loads AudioStreamPlayer2D as AudioStreamPlayer).
#pragma once

#include "godot/node.h"
#include "godot/script.h"

#include <string>
#include <vector>

namespace scripts {

// PlayShellDropSound is called from an ANIMATION METHOD TRACK (four keys in
// animation_methods.json) while the shells are loaded, never from script flow --
// hence the call() dispatcher. It also draws randi_range once per drop, so it
// is a live consumer of the global RNG stream.
class ShellDropManager : public godot::Script {
public:
    static constexpr const char* kClassName = "ShellDropManager";
    const char* get_class_name() const override { return kClassName; }

    godot::AudioStreamPlayer* speaker = nullptr;
    std::vector<std::string> soundArray;

    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    void PlayShellDropSound();   // gd: L6-L9
};

}  // namespace scripts
