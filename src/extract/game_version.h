// The game's version, as GlobalVariables.gd states it, and the versions
// supported.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace extract {

struct GameVersion {
    std::string number;   // "v2.2.0"
    int hotfix = -1;
};

std::optional<GameVersion> ParseGameVersion(std::string_view globalVariablesGd);
bool IsSupported(const GameVersion& v);
std::string Describe(const GameVersion& v);
std::string SupportedVersions();

}  // namespace extract
