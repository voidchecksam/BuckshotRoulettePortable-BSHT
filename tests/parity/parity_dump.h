// Parity dumps: what dump_state.gd writes from Godot and what bsht_parity
// builds from the C++ runtime, and the comparison between them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace parity {

struct NodeState {
    std::string path;
    double g[12] = {};          // global basis columns x, y, z, then origin
    bool v = true;              // own visible
    bool vt = true;             // is_visible_in_tree
    bool hasLayers = false;
    uint32_t l = 0;
    bool hasTransparency = false;
    double t = 0.0;
    bool hasAlbedo = false;   // surface 0's active BaseMaterial3D albedo
    double a[4] = {};
};

// One sampled value, keyed by an id both sides build the same way.
//
// `s` is the sample's NAME half, compared exactly: what the ray hit, which
// control has focus, the active branch's alias, the animation playing. Most of
// what the interaction scripts decide is a name, and no numeric tolerance can
// tell "the port focused nothing" from "the port focused the gun".
struct Sample {
    std::string id;
    std::vector<double> v;
    std::string s;
};

struct Dump {
    std::string scenario;
    std::vector<NodeState> nodes;
    std::vector<double> rng;
    std::vector<std::string> events;
    std::vector<Sample> samples;
};

bool ReadDump(const std::string& path, Dump& out);
// The port's side, in the shape dump_state.gd writes (scenario, events,
// samples). Used to choose a seed: run the port, read what it dealt.
bool WriteDump(const std::string& path, const Dump& d);

// Tolerances: transforms 1e-4, rng 1e-9 (integers are exact in a double).
// Returns human-readable differences, at most `limit`; empty is equal.
std::vector<std::string> Compare(const Dump& reference, const Dump& actual, std::size_t limit);

}  // namespace parity
