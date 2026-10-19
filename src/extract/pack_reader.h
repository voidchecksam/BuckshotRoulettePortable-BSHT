// The directory of a Godot 4.1 pack: a bare .pck, or one appended to an
// executable. Mirrors PackedSourcePCK::try_open_pack in Godot 4.1.4.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace extract {

struct PackEntry {
    std::string path;       // as stored: "res://..."
    uint64_t offset = 0;    // absolute, in the file
    uint64_t size = 0;
    bool encrypted = false;
};

struct PackInfo {
    uint32_t format = 0;
    uint32_t major = 0, minor = 0, patch = 0;   // the engine that wrote it
    std::vector<PackEntry> entries;
};

enum class PackError { None, CannotOpen, NoPack, UnsupportedFormat, NewerEngine, Encrypted, Truncated };

const char* PackErrorText(PackError e);
PackError ReadPack(const std::string& utf8Path, PackInfo& out);
bool ReadEntry(const std::string& utf8Path, const PackEntry& e, std::vector<uint8_t>& out);
const PackEntry* FindEntry(const PackInfo& p, std::string_view resPath);

}  // namespace extract
