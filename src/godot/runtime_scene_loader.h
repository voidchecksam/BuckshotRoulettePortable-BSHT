// Builds a godot::SceneTree from assets/extracted/runtime_main.json, written by
// tools/godot/export_runtime_scene.gd from the live scene after _ready. It
// lives in godot/ because it constructs godot:: nodes, and godot/ depends on
// assets/, never the reverse.
//
// The export is post-_ready state, so the loader does not call ready(); the
// host binds the scripts and readies them afterwards (SceneTree::ready_scripts).
#pragma once

#include "godot/scene_tree.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace godot {

// 2: material, AnimationPlayer, AudioStreamPlayer and OmniLight3D state.
// 3: Control text / visible_characters / scale / modulate, Label3D text,
//    MeshInstance3D mesh name, BaseMaterial3D resources inlined in exports.
// 4: Control focus / neighbours / global transform / size / position / mouse_filter,
//    CollisionObject3D layer + mask, CollisionShape3D shape (+ <export>_faces.bin),
//    Camera3D near / far.
inline constexpr int kRuntimeSceneFormat = 4;

enum class RuntimeSceneError { None = 0, FileNotFound, BadFormat, BadVersion, BadParent };

const char* RuntimeSceneErrorText(RuntimeSceneError e);

RuntimeSceneError LoadRuntimeScene(const char* path, SceneTree& tree);
// `faces`: the concave collision triangles (<export>_faces.bin); nullptr loads
// concave shapes empty, with a warning.
RuntimeSceneError LoadRuntimeSceneFromString(std::string_view json, SceneTree& tree,
                                             const std::vector<float>* faces = nullptr);

// One exported node, built with its state but placed in no tree. `parent` is an
// index into the same list (-1 for the root), `path` the exported path.
struct BuiltNode {
    std::unique_ptr<Node> node;
    int parent = -1;
    std::string path;
};

// The shared half of LoadRuntimeSceneFromString and PackedScene::instantiate.
// `rootName` is the export's "root_name" ("main" when absent).
RuntimeSceneError BuildRuntimeNodes(std::string_view json, std::vector<BuiltNode>& out,
                                    std::string& rootName, const std::vector<float>* faces = nullptr);

// Gives every AnimationPlayer node its animations, by player path. `library`
// must outlive `tree`. Players the export left playing resume. Returns how many
// players were bound.
int BindAnimationLibrary(SceneTree& tree, const assets::AnimationLibrary& library);

// One exported BaseMaterial3D object ({"albedo":[..],"transparency":n,...}),
// braces included: the one material reader, used for surfaces, overrides and
// the materials scripts hold in their exports alike.
SurfaceMaterial ReadMaterialJson(std::string_view object);

}  // namespace godot
