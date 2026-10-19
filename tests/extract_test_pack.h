// Small Godot 4.1 packs, built in memory the way Godot's exporter writes them.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace testpack {

struct File {
    std::string path;     // "res://..."
    std::string data;
    uint32_t flags = 0;   // 1 = encrypted
};

struct Options {
    uint32_t format = 2, major = 4, minor = 1, patch = 1;
    uint32_t packFlags = 0;      // 1 = encrypted directory
    std::string prefix;          // bytes before the pack, like an executable
    bool trailer = false;        // size + magic at the end, as an embedded pack has
    uint32_t claimedCount = 0;   // nonzero: write this file count instead of the real one
};

inline void Put32(std::string& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(char(v >> (8 * i))); }
inline void Put64(std::string& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(char(v >> (8 * i))); }

inline std::string Build(const std::vector<File>& files, const Options& o = {}) {
    std::string dir;
    std::vector<uint64_t> offsets;
    uint64_t data = 0;
    for (const File& f : files) {
        offsets.push_back(data);
        data += f.data.size();
    }
    for (size_t i = 0; i < files.size(); ++i) {
        std::string p = files[i].path;
        while (p.size() % 4) p.push_back('\0');   // Godot pads paths to 4 bytes
        Put32(dir, uint32_t(p.size()));
        dir += p;
        Put64(dir, offsets[i]);
        Put64(dir, files[i].data.size());
        dir.append(16, '\0');                      // md5, unchecked
        Put32(dir, files[i].flags);
    }
    const uint64_t header = 4 + 4 + 12 + 4 + 8 + 64 + 4;
    const uint64_t fileBase = o.prefix.size() + header + dir.size();
    std::string pack;
    Put32(pack, 0x43504447);   // "GDPC"
    Put32(pack, o.format);
    Put32(pack, o.major);
    Put32(pack, o.minor);
    Put32(pack, o.patch);
    Put32(pack, o.packFlags);
    Put64(pack, fileBase);
    pack.append(64, '\0');
    Put32(pack, o.claimedCount ? o.claimedCount : uint32_t(files.size()));
    pack += dir;
    for (const File& f : files) pack += f.data;
    std::string out = o.prefix + pack;
    if (o.trailer) {
        Put64(out, pack.size());
        Put32(out, 0x43504447);
    }
    return out;
}

inline std::filesystem::path Write(const std::filesystem::path& p, const std::string& bytes) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << bytes;
    return p;
}

}  // namespace testpack
