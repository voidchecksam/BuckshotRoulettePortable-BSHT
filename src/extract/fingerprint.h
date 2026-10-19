// What makes a copy of the game a known one: a SHA-256 over every file's path,
// size and own SHA-256, in path order.
#pragma once

#include "extract/pack_reader.h"

#include <string>
#include <string_view>

namespace extract {

struct KnownBuild {
    const char* name;          // shown to the player
    const char* fingerprint;   // 64 hex characters
};

std::string PackFingerprint(const std::string& packPath, const PackInfo& pack, std::string& error);
const KnownBuild* FindKnownBuild(std::string_view fingerprint);

}  // namespace extract
