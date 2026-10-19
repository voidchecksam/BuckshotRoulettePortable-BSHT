// PackedScene: a scene template instantiated at runtime (instances/shell.tscn
// for ShellSpawner.SpawnShells). Loaded from the same runtime scene export as
// main (tools/godot/export_runtime_scene.gd, instanced mode), registered by its
// res:// path so the bindings can hand a script its `@export var x : PackedScene`.
//
// instantiate() builds the nodes detached: parent and child links are set and
// scripts are attached and bound (so a script may call into the clone before
// add_child, as SpawnShells does with ApplyStatus), but nothing is in a tree
// and nothing has readied. Node::add_child adopts the subtree and readies it.
#pragma once

#include "godot/node.h"

#include <memory>
#include <string>

namespace godot {

class PackedScene {
public:
    // Attaches and binds scripts on a freshly instantiated subtree. Installed by
    // scripts::RegisterScripts(); godot/ cannot depend on scripts/.
    using ScriptBinder = void (*)(Node* instanceRoot);
    static void set_script_binder(ScriptBinder binder);

    // Registers (or replaces the document of) the scene at `resPath`. nullptr
    // when the file or document is not a loadable runtime scene export. The
    // returned pointer stays valid for the life of the process.
    static PackedScene* load(const std::string& resPath, const char* exportFile);
    static PackedScene* load_from_string(const std::string& resPath, std::string json);
    static PackedScene* find(const std::string& resPath);

    const std::string& get_path() const { return path_; }

    // A detached copy of the scene; nullptr if the document no longer parses.
    // MeshInstance3D clones carry the template key "<path without res://>:<root
    // name>[/<path in scene>]" that selects the baked template props.
    std::unique_ptr<Node> instantiate() const;

private:
    std::string path_;
    std::string json_;
};

}  // namespace godot
