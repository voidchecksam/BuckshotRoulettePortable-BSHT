#include "godot/script.h"

#include "godot/node.h"
#include "godot/scene_tree.h"

#include <utility>

namespace godot {
namespace {

std::vector<std::pair<std::string, ScriptFactory>>& Registry() {
    static std::vector<std::pair<std::string, ScriptFactory>> r;
    return r;
}

}  // namespace

SceneTree* Script::get_tree() const {
    return owner_ ? owner_->get_tree() : nullptr;
}

double Script::get_process_delta_time() const {
    const SceneTree* tree = get_tree();
    return tree ? tree->get_process_delta_time() : 0.0;
}

void register_script_class(std::string_view file, ScriptFactory factory) {
    for (auto& entry : Registry()) {
        if (entry.first == file) {
            entry.second = factory;
            return;
        }
    }
    Registry().emplace_back(std::string(file), factory);
}

std::unique_ptr<Script> create_script(std::string_view file) {
    for (const auto& entry : Registry()) {
        if (entry.first == file) return entry.second();
    }
    return nullptr;
}

}  // namespace godot
