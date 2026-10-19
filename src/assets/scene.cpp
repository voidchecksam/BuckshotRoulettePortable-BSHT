#include "assets/scene.h"

#include "core/json_lite.h"

#include <cstdio>
#include <unordered_map>

namespace assets {

namespace {

using core::json::FindArray;
using core::json::ReadBool;
using core::json::ReadInt;
using core::json::ReadNumberArray;
using core::json::ReadString;
using core::json::Span;
using core::json::SplitObjects;

// Reads a flat 16-number array into a column-major Mat4. Leaves the matrix as
// identity if the array is any other length: a partly filled transform would
// place geometry somewhere plausible but wrong, which is harder to notice than
// a node that did not move.
bool ReadMat4(const char* begin, const char* end, const char* key, core::Mat4& out) {
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!FindArray(begin, end, key, &ab, &ae)) return false;

    std::vector<double> v;
    ReadNumberArray(ab, ae, v);
    if (v.size() != 16) return false;

    for (int i = 0; i < 16; ++i) out.m[i] = static_cast<float>(v[static_cast<std::size_t>(i)]);
    return true;
}

// "a/b/c" -> "a/b"; "a" -> "." (the root); "." -> "" (no parent).
std::string ParentPath(const std::string& path) {
    if (path == ".") return std::string();
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

}  // namespace

const core::Mat4& Scene::WorldTransform(int index) const {
    if (index < 0 || index >= static_cast<int>(world_.size())) return identity_;
    return world_[static_cast<std::size_t>(index)];
}

const core::Mat4& Scene::LightWorldTransform(int i) const {
    if (i < 0 || i >= static_cast<int>(lights_.size())) return identity_;
    const OmniLight& l = lights_[static_cast<std::size_t>(i)];
    if (l.node >= 0) return WorldTransform(l.node);
    return l.local;
}

int Scene::FindByPath(const char* path) const {
    if (!path) return -1;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].path == path) return static_cast<int>(i);
    }
    return -1;
}

void Scene::BuildHierarchy() {
    std::unordered_map<std::string, int> byPath;
    byPath.reserve(nodes_.size() * 2);
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        // First occurrence wins. Godot allows sibling name collisions to be
        // disambiguated, so a duplicate path here would be an export bug; taking
        // the first keeps resolution deterministic either way.
        byPath.emplace(nodes_[i].path, static_cast<int>(i));
    }

    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const std::string pp = ParentPath(nodes_[i].path);
        if (pp.empty()) { nodes_[i].parent = -1; continue; }
        const auto it = byPath.find(pp);
        nodes_[i].parent = (it == byPath.end()) ? -1 : it->second;
        // A node cannot be its own parent; that would spin the walk below.
        if (nodes_[i].parent == static_cast<int>(i)) nodes_[i].parent = -1;
    }

    // Compose world transforms. The export is a pre-order walk so parents
    // normally precede children, but that is not relied on: each node is
    // resolved on demand and memoised, with a depth cap that makes a cyclic or
    // self-referential parent chain terminate instead of hanging.
    world_.assign(nodes_.size(), core::Mat4{});
    std::vector<char> done(nodes_.size(), 0);

    const std::size_t maxDepth = nodes_.size() + 1;
    std::vector<int> chain;
    chain.reserve(32);

    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (done[i]) continue;

        chain.clear();
        int cur = static_cast<int>(i);
        while (cur >= 0 && !done[static_cast<std::size_t>(cur)] &&
               chain.size() < maxDepth) {
            chain.push_back(cur);
            cur = nodes_[static_cast<std::size_t>(cur)].parent;
        }

        // Walk back down, applying each local onto its parent's world.
        for (std::size_t k = chain.size(); k-- > 0;) {
            const int idx = chain[k];
            const SceneNode& n = nodes_[static_cast<std::size_t>(idx)];
            const core::Mat4& localOrIdentity = n.local;   // identity if absent
            if (n.parent >= 0) {
                world_[static_cast<std::size_t>(idx)] =
                    core::Mul(world_[static_cast<std::size_t>(n.parent)], localOrIdentity);
            } else {
                world_[static_cast<std::size_t>(idx)] = localOrIdentity;
            }
            done[static_cast<std::size_t>(idx)] = 1;
        }
    }
}

bool Scene::LoadFromString(const char* json) {
    nodes_.clear();
    lights_.clear();
    world_.clear();
    if (!json) return false;

    const char* begin = json;
    const char* end = json + std::char_traits<char>::length(json);

    const char* nb = nullptr;
    const char* ne = nullptr;
    if (!FindArray(begin, end, "nodes", &nb, &ne)) return false;

    std::vector<Span> objs;
    SplitObjects(nb, ne, objs);
    if (objs.empty()) return false;

    nodes_.reserve(objs.size());
    for (const Span& o : objs) {
        SceneNode n;
        n.name    = ReadString(o.first, o.second, "name");
        n.path    = ReadString(o.first, o.second, "path");
        n.type    = ReadString(o.first, o.second, "type");
        n.meshId  = ReadString(o.first, o.second, "mesh_id");
        n.surfaces = ReadInt(o.first, o.second, "surfaces", 0);
        n.visible  = ReadBool(o.first, o.second, "visible", true);
        // Defaults to `visible` so an OLD export without the field keeps
        // its previous behaviour rather than hiding the whole room.
        n.visibleInTree =
            ReadBool(o.first, o.second, "visible_in_tree", n.visible);
        n.skinned  = ReadBool(o.first, o.second, "skinned", false);
        n.hasTransform = ReadMat4(o.first, o.second, "transform", n.local);
        if (n.path.empty()) continue;      // a node without a path cannot be placed
        nodes_.push_back(std::move(n));
    }
    if (nodes_.empty()) return false;

    const char* lb = nullptr;
    const char* le = nullptr;
    if (FindArray(begin, end, "lights", &lb, &le)) {
        objs.clear();
        SplitObjects(lb, le, objs);
        lights_.reserve(objs.size());
        for (const Span& o : objs) {
            OmniLight l;
            l.path   = ReadString(o.first, o.second, "path");
            l.energy = static_cast<float>(
                core::json::ReadNumber(o.first, o.second, "energy", 1.0));
            l.range  = static_cast<float>(
                core::json::ReadNumber(o.first, o.second, "range", 1.0));
            l.attenuation = static_cast<float>(
                core::json::ReadNumber(o.first, o.second, "attenuation", 0.0));
            ReadMat4(o.first, o.second, "transform", l.local);

            const char* cb = nullptr;
            const char* ce = nullptr;
            if (FindArray(o.first, o.second, "color", &cb, &ce)) {
                std::vector<double> c;
                ReadNumberArray(cb, ce, c);
                for (std::size_t i = 0; i < 3 && i < c.size(); ++i) {
                    l.colour[i] = static_cast<float>(c[i]);
                }
            }
            lights_.push_back(std::move(l));
        }
    }

    BuildHierarchy();

    // Resolve each light to its own node so its world transform can be
    // composed. The exporter writes a LOCAL transform, so this is required for
    // correct placement, not an optimisation.
    for (OmniLight& l : lights_) {
        l.node = FindByPath(l.path.c_str());
    }

    return true;
}

bool Scene::LoadFromFile(const char* path) {
    nodes_.clear();
    lights_.clear();
    world_.clear();
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
