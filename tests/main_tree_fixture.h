// The real main.tscn tree for script tests: loaded from the runtime export,
// animations and scripts bound, not readied. Each test decides whether the
// intro runs (ready_scripts as is) or is skipped (scripts::SkipIntro first).
// Skips (ok == false) until the exports exist.
#pragma once

#include "assets/animation.h"
#include "godot/animation_player.h"
#include "godot/input_map.h"
#include "godot/material.h"
#include "godot/packed_scene.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/bind_support.h"
#include "scripts/item_scenes.h"

#include <cstdio>
#include <string>
#include <vector>

namespace testing_support {

inline std::string Extracted(const char* file) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + file;
}

inline bool FileExists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

struct MainTree {
    assets::AnimationLibrary animations;   // before the tree: players point into it
    godot::SceneTree tree;
    std::vector<std::string> bindErrors;
    bool ok = false;

    MainTree() {
        // A fresh scene loads fresh resources (Godot frees a .tres when its
        // last scene goes): a previous test's material writes must not leak.
        godot::MaterialLibrary::reset();
        for (const char* f : {"runtime_main.json", "runtime_shell.json", "animations.json", "dialogue_en.json",
                              "input_map.json"}) {
            if (!FileExists(Extracted(f))) return;
        }
        godot::set_translation_root(std::string(BSHT_SOURCE_DIR) + "/assets/extracted");
        godot::set_locale("EN");
        godot::InputMap::get_singleton().load(Extracted("input_map.json").c_str());
        scripts::RegisterScripts();
        if (!godot::PackedScene::load("res://instances/shell.tscn", Extracted("runtime_shell.json").c_str())) return;
        if (godot::LoadRuntimeScene(Extracted("runtime_main.json").c_str(), tree) != godot::RuntimeSceneError::None) {
            return;
        }
        if (godot::Node* itemManager = tree.get_node("standalone managers/item manager")) {
            for (const std::string& res : scripts::ItemScenePaths(*itemManager)) {
                const std::string file = scripts::ItemSceneExportFile(res);
                if (!godot::PackedScene::load(res, Extracted(file.c_str()).c_str())) return;
            }
        }
        if (!animations.LoadFromFile(Extracted("animations.json").c_str())) return;
        godot::BindAnimationLibrary(tree, animations);
        bindErrors = scripts::BindScripts(tree.get_root()).errors;
        ok = true;
    }

    // Godot's frame at 1/60 s: the physics step (deferred calls, frees), then process.
    void Run(double seconds) {
        const int frames = static_cast<int>(seconds * 60.0 + 0.5);
        for (int i = 0; i < frames; ++i) tree.process_frame(1.0 / 60.0);
    }
};

}  // namespace testing_support
