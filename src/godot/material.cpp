#include "godot/material.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace godot {

namespace {

// unique_ptr: the pointers handed out must survive the map rehashing.
std::unordered_map<std::string, std::unique_ptr<SurfaceMaterial>>& Library() {
    static std::unordered_map<std::string, std::unique_ptr<SurfaceMaterial>> library;
    return library;
}

// Materials with no resource path: one object per request.
std::vector<std::unique_ptr<SurfaceMaterial>>& Unshared() {
    static std::vector<std::unique_ptr<SurfaceMaterial>> unshared;
    return unshared;
}

}  // namespace

SurfaceMaterial* MaterialLibrary::get(const std::string& res, const SurfaceMaterial& initial) {
    if (res.empty()) return Unshared().emplace_back(std::make_unique<SurfaceMaterial>(initial)).get();
    auto& lib = Library();
    auto it = lib.find(res);
    if (it == lib.end()) it = lib.emplace(res, std::make_unique<SurfaceMaterial>(initial)).first;
    return it->second.get();
}

void MaterialLibrary::reset() {
    Library().clear();
    Unshared().clear();
}

std::string TextureKeyForResource(const std::string& res) {
    if (res.empty()) return std::string();
    constexpr const char kPrefix[] = "res://";
    std::string name = res.rfind(kPrefix, 0) == 0 ? res.substr(sizeof(kPrefix) - 1) : res;
    for (char& c : name) {
        if (c == '/' || c == ':' || c == ' ') c = '_';
    }
    constexpr const char kPng[] = ".png";
    const std::size_t n = sizeof(kPng) - 1;
    if (name.size() < n || name.compare(name.size() - n, n, kPng) != 0) name += kPng;
    return "inline_textures/" + name;
}

}  // namespace godot
