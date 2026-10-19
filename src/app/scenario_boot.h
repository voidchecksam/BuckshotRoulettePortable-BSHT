// A scenario the device boots into. An optional <saveDir>/scenario.json
// (copied into Vita3K's ux0:data/bsht, or onto a Vita with VitaShell) makes the
// Vita build replay a parity scenario as `bsht_desktop --scenario <file>`
// does: it starts at the scenario's scene, drives the same timeline at 1/60 s,
// and writes each shot beside the file. No file (the player's boot) changes
// nothing. The entry point logs "scenario: <path>" when one is found.
#pragma once

#include <optional>
#include <string>

namespace app {

// The text of <saveDir>/scenario.json, or nullopt when it is missing or empty.
std::optional<std::string> FindDeviceScenario(const std::string& saveDir);

// Where a scenario's shot `at` goes: <dir>/port_<name>_<at>.ppm, the name the
// desktop writes under build/shots, so the two compare file for file.
std::string ScenarioShotPath(const std::string& dir, const std::string& name, int at);

}  // namespace app
