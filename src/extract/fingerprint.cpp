#include "extract/fingerprint.h"

#include <picosha2.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace extract {

namespace {

// From `bsht-extract --fingerprint <exe>` on a checked copy.
constexpr KnownBuild kKnownBuilds[] = {
    // Buckshot Roulette.exe, 796,325,472 bytes, the Steam build.
    {"v2.2.0 hotfix 6 (Steam)", "07cbbf0adb2cd2b7f31ddfb10bd8861d09a0e7fffd34380fa74de47c46d3fdd6"},
};

}  // namespace

std::string PackFingerprint(const std::string& packPath, const PackInfo& pack, std::string& error) {
    std::ifstream in(std::filesystem::path(std::u8string(packPath.begin(), packPath.end())), std::ios::binary);
    if (!in) {
        error = "cannot open " + packPath;
        return {};
    }
    std::vector<const PackEntry*> order;
    for (const PackEntry& e : pack.entries) order.push_back(&e);
    std::sort(order.begin(), order.end(), [](const PackEntry* a, const PackEntry* b) { return a->path < b->path; });

    std::string lines;
    std::vector<char> buf(1 << 20);
    for (const PackEntry* e : order) {
        picosha2::hash256_one_by_one h;
        in.seekg(static_cast<std::streamoff>(e->offset));
        uint64_t left = e->size;
        while (left > 0) {
            const size_t n = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
            in.read(buf.data(), static_cast<std::streamsize>(n));
            if (!in) {
                error = "cannot read " + e->path;
                return {};
            }
            h.process(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
            left -= n;
        }
        h.finish();
        std::string hex;
        picosha2::get_hash_hex_string(h, hex);
        lines += e->path + "\t" + std::to_string(e->size) + "\t" + hex + "\n";
    }
    return picosha2::hash256_hex_string(lines);
}

const KnownBuild* FindKnownBuild(std::string_view fingerprint) {
    for (const KnownBuild& b : kKnownBuilds) {
        if (fingerprint == b.fingerprint) return &b;
    }
    return nullptr;
}

}  // namespace extract
