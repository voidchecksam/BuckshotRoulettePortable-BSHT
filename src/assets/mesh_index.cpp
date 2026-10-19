#include "assets/mesh_index.h"

#include "core/json_lite.h"

#include <cstdio>

namespace assets {

namespace {

using core::json::FindArray;
using core::json::ReadString;
using core::json::ReadStringArray;
using core::json::Span;
using core::json::SplitObjects;

}  // namespace

bool MeshIndex::LoadFromString(const char* json) {
    byName_.clear();
    modelCount_ = 0;
    if (!json) return false;

    const char* begin = json;
    const char* end = json + std::char_traits<char>::length(json);

    const char* mb = nullptr;
    const char* me = nullptr;
    if (!FindArray(begin, end, "models", &mb, &me)) return false;

    std::vector<Span> models;
    SplitObjects(mb, me, models);
    if (models.empty()) return false;

    for (const Span& m : models) {
        const std::string glb = ReadString(m.first, m.second, "glb");
        if (glb.empty()) continue;
        ++modelCount_;

        const char* nb = nullptr;
        const char* ne = nullptr;
        if (!FindArray(m.first, m.second, "meshes", &nb, &ne)) continue;

        std::vector<std::string> names;
        ReadStringArray(nb, ne, names);

        for (std::size_t i = 0; i < names.size(); ++i) {
            // Empty entries are meshes that failed to join. They are kept in the
            // array so the INDEX stays aligned with the glTF mesh index; they
            // just do not become lookups.
            if (names[i].empty()) continue;

            MeshRef ref;
            ref.glb = glb;
            ref.meshIndex = static_cast<int>(i);

            // Names are unique across the library; if two ever clash, the first
            // one wins, so resolution does not depend on load order.
            byName_.emplace(names[i], ref);
        }
    }

    return !byName_.empty();
}

MeshRef MeshIndex::Resolve(const std::string& meshId) const {
    const auto it = byName_.find(meshId);
    if (it == byName_.end()) return MeshRef{};
    return it->second;
}

bool MeshIndex::LoadFromFile(const char* path) {
    byName_.clear();
    modelCount_ = 0;
    if (!path) return false;

    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }

    std::string buf;
    buf.resize(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(&buf[0], 1, static_cast<std::size_t>(n), f);
    std::fclose(f);
    buf.resize(got);

    return LoadFromString(buf.c_str());
}

}  // namespace assets
