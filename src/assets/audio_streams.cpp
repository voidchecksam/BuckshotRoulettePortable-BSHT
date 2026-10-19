#include "assets/audio_streams.h"

#include "core/json_lite.h"

#include <cstdio>
#include <vector>

namespace assets {

bool AudioStreams::Parse(const std::string& json) {
    using namespace core::json;
    byName_.clear();
    const char* b = json.data();
    const char* e = b + json.size();
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!FindArrayTopLevel(b, e, "streams", &ab, &ae)) return false;
    std::vector<Span> items;
    SplitObjects(ab, ae, items);
    for (const Span& it : items) {
        const std::string name = ReadStringTopLevelDecoded(it.first, it.second, "name");
        if (name.empty()) continue;
        AudioStreamInfo info;
        info.length = static_cast<float>(ReadNumberTopLevel(it.first, it.second, "length", 0.0));
        info.loop = ReadBoolTopLevel(it.first, it.second, "loop", false);
        byName_[name] = info;
    }
    return true;
}

bool AudioStreams::LoadFromFile(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::string json;
    char buf[8192];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) json.append(buf, n);
    std::fclose(f);
    return Parse(json);
}

const AudioStreamInfo* AudioStreams::Find(const std::string& name) const {
    const auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : &it->second;
}

}  // namespace assets
