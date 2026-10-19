// source: scripts/PipelineManager.gd (30 lines)
// PORT deviations:
//   - The reference copy holds tools/godot/pipeline_manager.gd (the headless
//     stand-in); this follows v2.2's own script.
//   - using_gl is always false: the port draws like the Vulkan reference,
//     so the GL branches (lights halved, nodes hidden) never run.
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

class PipelineManager : public godot::Script {
public:
    static constexpr const char* kClassName = "PipelineManager";
    const char* get_class_name() const override { return kClassName; }

    godot::WorldEnvironment* env = nullptr;
    godot::Node* cam = nullptr;
    std::string scene;
    godot::Control* overlay = nullptr;
    // PORT: @export main_array_lights : Array[Light3D] (type not supported)
    std::vector<godot::Node3D*> main_array_hide;
    std::vector<godot::Node3D*> main_array_toggle;
    godot::Node3D* main_restroom = nullptr;
    // PORT: @export main_light_tabletop2 : Light3D (type not supported)
    // PORT: @export main_light_tabletop : Light3D (type not supported)

    void _ready() override;
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;
    void CheckStatus();
    void Toggle();
    void AdjustSettings(const std::string& scene);
};

}  // namespace scripts
