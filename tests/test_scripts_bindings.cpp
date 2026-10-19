// The generated bindings against the REAL runtime export: every translated
// script is created on its node and every @export resolves. Skips until
// export_runtime_scene.gd has been run.
#include "scripts/bind_support.h"
#include "scripts/CameraManager.h"
#include "scripts/DialogueManager.h"
#include "scripts/SegmentManager.h"
#include "scripts/SpeakerController.h"
#include "scripts/resources.h"

#include "godot/packed_scene.h"
#include "godot/runtime_scene_loader.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {

std::string ExportPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_main.json";
}

bool HaveExport() {
    std::FILE* f = std::fopen(ExportPath().c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

}  // namespace

TEST(ScriptsBindings, EveryTranslatedScriptBindsToTheRealScene) {
    if (!HaveExport()) GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd";
    godot::SceneTree tree;
    ASSERT_EQ(godot::LoadRuntimeScene(ExportPath().c_str(), tree), godot::RuntimeSceneError::None);
    scripts::RegisterScripts();
    // As GameApp does: scenes a script exports must be registered before binding.
    // The registry is process-wide, and ctest runs this test in its own process.
    const std::string shellPath = std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_shell.json";
    ASSERT_NE(godot::PackedScene::load("res://instances/shell.tscn", shellPath.c_str()), nullptr);

    const scripts::BindResult r = scripts::BindScripts(tree.get_root());

    for (const std::string& e : r.errors) ADD_FAILURE() << e;
    EXPECT_GE(r.created, 5);
    EXPECT_EQ(r.created, r.bound);

    auto* dialogue = godot::script_cast<scripts::Dialogue>(tree.get_node("standalone managers/dialogue manager"));
    ASSERT_NE(dialogue, nullptr);
    EXPECT_DOUBLE_EQ(dialogue->incrementDelay, 0.02);
    ASSERT_EQ(dialogue->soundArray_clicks.size(), 4u);
    EXPECT_EQ(dialogue->soundArray_clicks[3], "dialogue click4.ogg");
    EXPECT_EQ(dialogue->dialogueUI, tree.get_node("Camera/dialogue UI/main dialogue/dialogue text"));

    auto* camera = godot::script_cast<scripts::CameraManager>(tree.get_node("standalone managers/camera manager"));
    ASSERT_NE(camera, nullptr);
    EXPECT_EQ(camera->cam, tree.get_current_camera());
    ASSERT_EQ(camera->socketArray.size(), 25u);   // v2.2 adds two
    EXPECT_EQ(camera->socketArray[0]->socketName, "home");
    EXPECT_DOUBLE_EQ(camera->dur, 0.6);

    auto* music = godot::script_cast<scripts::SpeakerController>(
        tree.get_node("speaker parent/speaker_main music/speaker controller_main music"));
    ASSERT_NE(music, nullptr);
    EXPECT_DOUBLE_EQ(music->fadeDuration, 5.0);
}

// SegmentManager.mat is the cut segment's material_override (both are
// res://materials/overwrite.tres): fading one must fade the other, as in Godot.
TEST(ScriptsBindings, TheSegmentManagersMaterialIsTheSegmentsOverride) {
    if (!HaveExport()) GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd";
    godot::SceneTree tree;
    ASSERT_EQ(godot::LoadRuntimeScene(ExportPath().c_str(), tree), godot::RuntimeSceneError::None);
    scripts::RegisterScripts();
    scripts::BindScripts(tree.get_root());
    auto* manager = godot::script_cast<scripts::SegmentManager>(tree.get_node(
        "tabletop parent/shotgun main parent/shotgun parent new/shotgun cut segment/segment manager"));
    const auto* segment = tree.get_node_as<godot::GeometryInstance3D>(
        "tabletop parent/shotgun main parent/shotgun parent new/shotgun cut segment");
    ASSERT_NE(manager, nullptr);
    ASSERT_NE(segment, nullptr);
    ASSERT_NE(manager->mat, nullptr);
    manager->mat->albedo = {1.0f, 1.0f, 1.0f, 0.0f};
    EXPECT_FLOAT_EQ(segment->get_draw_alpha(), 0.0f);
}
