// The frame_order probe, transcribed from the Probe class in
// tools/godot/dump_state.gd. Any change there changes here.
#pragma once

#include <string>
#include <vector>

namespace parity {

// Builds A{B{D}, C} under a plain root, calls ready(), runs `frames` frames at
// 1/60 s, and returns the event log in dump_state.gd's format.
std::vector<std::string> RunFrameOrderProbe(int frames);

// The SemProbe tree from tools/godot/dump_state.gd (AP, T, Z), driven by
// the scenario's steps for its frame count, logging in dump_state.gd's format.
std::vector<std::string> RunAnimPlaySemanticsProbe(const std::string& scenarioJson);

}  // namespace parity
