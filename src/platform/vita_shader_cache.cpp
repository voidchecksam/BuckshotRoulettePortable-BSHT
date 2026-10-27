#include "platform/vita_shader_cache.h"

#include "platform/shader_cache_check.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <cstdio>
#include <initializer_list>
#include <vector>

namespace platform {
namespace {

// A cached shader is a few KB; anything far bigger is not one.
constexpr SceOff kMaxCachedShader = 1 << 20;

bool FileIsWhole(const std::string& path) {
    const SceUID f = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (f < 0) return false;
    const SceOff size = sceIoLseek(f, 0, SCE_SEEK_END);
    bool whole = false;
    if (size > 0 && size <= kMaxCachedShader) {
        std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
        sceIoLseek(f, 0, SCE_SEEK_SET);
        const int got = sceIoRead(f, bytes.data(), static_cast<SceSize>(bytes.size()));
        whole = got == static_cast<int>(bytes.size()) && CachedShaderIsWhole(bytes.data(), bytes.size());
    }
    sceIoClose(f);
    return whole;
}

}  // namespace

std::string PruneShaderCache(const char* titleId) {
    const std::string root = std::string("ux0:data/shader_cache/") + titleId + "/v1/";
    int seen = 0;
    int removed = 0;
    for (const char* kind : {"v", "f"}) {
        const std::string dir = root + kind;
        const SceUID d = sceIoDopen(dir.c_str());
        if (d < 0) continue;
        std::vector<std::string> bad;
        SceIoDirent entry;
        while (sceIoDread(d, &entry) > 0) {
            if (SCE_S_ISDIR(entry.d_stat.st_mode)) continue;
            ++seen;
            const std::string path = dir + "/" + entry.d_name;
            if (!FileIsWhole(path)) bad.push_back(path);
        }
        sceIoDclose(d);
        for (const std::string& path : bad) {
            if (sceIoRemove(path.c_str()) >= 0) ++removed;
        }
    }
    char line[160];
    std::snprintf(line, sizeof(line), "shader cache: %d files in %s, %d damaged removed", seen, root.c_str(),
                  removed);
    return line;
}

}  // namespace platform
