#include "godot/packed_scene.h"

#include "godot/runtime_scene_loader.h"

#include <cstdio>
#include <utility>
#include <vector>

namespace godot {
namespace {

std::vector<std::unique_ptr<PackedScene>>& Registry() {
    static std::vector<std::unique_ptr<PackedScene>> r;
    return r;
}

PackedScene::ScriptBinder& Binder() {
    static PackedScene::ScriptBinder b = nullptr;
    return b;
}

bool ReadFile(const char* path, std::string& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[65536];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

}  // namespace

void PackedScene::set_script_binder(ScriptBinder binder) {
    Binder() = binder;
}

PackedScene* PackedScene::find(const std::string& resPath) {
    for (const auto& s : Registry()) {
        if (s->path_ == resPath) return s.get();
    }
    return nullptr;
}

PackedScene* PackedScene::load_from_string(const std::string& resPath, std::string json) {
    std::vector<BuiltNode> probe;
    std::string rootName;
    if (BuildRuntimeNodes(json, probe, rootName) != RuntimeSceneError::None) return nullptr;
    PackedScene* scene = find(resPath);
    if (!scene) {
        Registry().push_back(std::make_unique<PackedScene>());
        scene = Registry().back().get();
        scene->path_ = resPath;
    }
    scene->json_ = std::move(json);
    return scene;
}

PackedScene* PackedScene::load(const std::string& resPath, const char* exportFile) {
    std::string json;
    if (!ReadFile(exportFile, json)) return nullptr;
    return load_from_string(resPath, std::move(json));
}

std::unique_ptr<Node> PackedScene::instantiate() const {
    std::vector<BuiltNode> built;
    std::string rootName;
    if (BuildRuntimeNodes(json_, built, rootName) != RuntimeSceneError::None || built.empty()) {
        return nullptr;
    }
    const std::string prefix = path_.rfind("res://", 0) == 0 ? path_.substr(6) : path_;

    std::vector<Node*> raw;
    raw.reserve(built.size());
    for (BuiltNode& b : built) raw.push_back(b.node.get());
    for (std::size_t i = 0; i < built.size(); ++i) {
        Node* n = raw[i];
        if (auto* mesh = cast_to<MeshInstance3D>(n)) {
            mesh->set_template_key(prefix + ":" + (i == 0 ? rootName : rootName + "/" + built[i].path));
        }
        if (built[i].parent >= 0) {
            Node* parent = raw[static_cast<std::size_t>(built[i].parent)];
            n->parent_ = parent;
            parent->children_.push_back(n);
        }
    }

    std::unique_ptr<Node> root = std::move(built[0].node);
    for (std::size_t i = 1; i < built.size(); ++i) root->detached_.push_back(std::move(built[i].node));
    if (Binder()) Binder()(root.get());
    return root;
}

}  // namespace godot
