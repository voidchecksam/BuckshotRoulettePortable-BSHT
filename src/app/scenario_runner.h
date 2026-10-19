// Drives a parity scenario (tests/parity/scenarios/*.json) on a live tree,
// frame by frame, as bsht_parity does. The checker and the desktop exe's
// --scenario mode share this one driver, so the exe renders the very state a
// parity run compared against Godot. Headless: links no GL.
//
// The frame protocol, per frame, mirroring Godot's main loop:
//   BeginFrame()  -> Seeded/Waiting: just run process_frame.
//                 -> Running: input was flushed and the physics step ran
//                    (the point Godot's MainLoop::process sees). Sample here.
//   EndFrame()    -> parses the timeline events due this frame; false once
//                    the scenario is over. Then run process_frame.
#pragma once

#include "core/json_lite.h"
#include "godot/input.h"
#include "godot/scene_tree.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace app {

// The scenario file's text, or empty when it cannot be read.
std::string ReadScenarioFile(const char* path);

class ScenarioRunner {
public:
    enum class Step { Seeded, Waiting, Running };

    // False when the scenario has no gate to wait for.
    bool Parse(std::string json);

    Step BeginFrame(godot::SceneTree& tree, godot::Input& input);
    bool EndFrame(godot::Input& input);

    int frame() const { return frame_; }        // Godot's frame number
    int at() const { return frame_ - zero_; }   // frames since the gate; valid when Running
    bool gateReached() const { return zero_ >= 0; }
    const std::string& name() const { return name_; }
    // "main" (default) or "menu": which scene the scenario runs in. A menu
    // scenario only seeds at its start frame (dump_state.gd _step_menu); the
    // exe boots the menu for one with --keep-menu. "intro" runs main with its
    // intro; "ending" runs main from the table and clears the last batch.
    const std::string& scene() const { return scene_; }
    const std::vector<int>& shots() const { return shots_; }   // `at` values to render

private:
    void Seed(godot::SceneTree& tree);

    std::string json_;
    std::string name_;
    std::string scene_ = "main";
    std::string gatePath_;
    std::vector<core::json::Span> steps_;
    std::vector<int> shots_;
    std::unordered_map<int, godot::Vector2> touchAt_;
    int start_ = 3;
    int after_ = 90;
    int last_ = 0;
    int frame_ = 0;
    int zero_ = -1;
    bool seeded_ = false;
};

}  // namespace app
