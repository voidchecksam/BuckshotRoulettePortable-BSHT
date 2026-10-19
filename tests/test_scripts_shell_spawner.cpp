// scripts/ShellSpawner.gd and scripts/ShellBranch.gd translated, on the REAL
// exports: real PackedScene instances of instances/shell.tscn land in the
// compartment in sequence order, coloured by ShellClass.ApplyStatus. Skips
// until export_runtime_scene.gd has written runtime_main.json and
// runtime_shell.json.
#include "scripts/DealerIntelligence.h"
#include "scripts/GlobalVariables.h"
#include "scripts/ShellBranch.h"
#include "scripts/ShellSpawner.h"
#include "scripts/bind_support.h"

#include "godot/array_ops.h"
#include "godot/packed_scene.h"
#include "godot/rng.h"
#include "godot/runtime_scene_loader.h"

#include "scripts/intro_skip.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string Extracted(const char* file) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + file;
}

bool Exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

constexpr const char* kShellScene = "res://instances/shell.tscn";

struct Loaded {
    godot::SceneTree tree;
    scripts::ShellSpawner* spawner = nullptr;
    godot::Node3D* compartment = nullptr;
    bool ok = false;

    Loaded() {
        if (!Exists(Extracted("runtime_main.json")) || !Exists(Extracted("runtime_shell.json"))) return;
        scripts::RegisterScripts();
        scripts::InstanceBindErrors().clear();
        if (!godot::PackedScene::load(kShellScene, Extracted("runtime_shell.json").c_str())) return;
        if (godot::LoadRuntimeScene(Extracted("runtime_main.json").c_str(), tree) != godot::RuntimeSceneError::None) {
            return;
        }
        const scripts::BindResult r = scripts::BindScripts(tree.get_root());
        for (const std::string& e : r.errors) ADD_FAILURE() << e;
        tree.ready_scripts();
        scripts::SkipIntro(tree);   // start at the table, as the parity scenarios do
        spawner = godot::script_cast<scripts::ShellSpawner>(tree.get_node("standalone managers/shell spawner"));
        compartment = tree.get_node_as<godot::Node3D>("tabletop parent/main tabletop/compartment_shells");
        ok = spawner && compartment;
    }
};

godot::MeshInstance3D* Body(godot::Node* shell) {
    return godot::cast_to<godot::MeshInstance3D>(shell);
}

}  // namespace

TEST(ScriptsShellClass, ApplyStatusRecoloursTheBodysSecondSurfaceOnItsOwnClone) {
    Loaded l;
    if (!l.ok) GTEST_SKIP() << "run export_runtime_scene.gd (main and instances/shell.tscn)";
    const godot::PackedScene* scene = godot::PackedScene::find(kShellScene);
    ASSERT_NE(scene, nullptr);

    std::unique_ptr<godot::Node> a = scene->instantiate();
    std::unique_ptr<godot::Node> b = scene->instantiate();
    auto* branchA = godot::script_cast<scripts::ShellClass>(a->get_child(0));
    auto* branchB = godot::script_cast<scripts::ShellClass>(b->get_child(0));
    ASSERT_NE(branchA, nullptr);
    ASSERT_NE(branchB, nullptr);
    EXPECT_EQ(branchA->mesh, a.get());   // `mesh = ..` binds to ITS clone
    EXPECT_EQ(branchB->mesh, b.get());

    branchA->isLive = true;
    branchA->ApplyStatus();
    branchB->isLive = false;
    branchB->ApplyStatus();

    // The tree's ShellEjectManager._ready has coloured the shared materials.
    EXPECT_FLOAT_EQ(Body(a.get())->get_active_surface_albedo(1).g, scripts::GlobalVariables::default_color_live.g);
    EXPECT_NEAR(Body(b.get())->get_active_surface_albedo(1).b, 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(Body(b.get())->get_active_surface_albedo(1).r, scripts::GlobalVariables::default_color_blank.r);
    EXPECT_FLOAT_EQ(Body(b.get())->get_active_surface_albedo(0).r, 1.0f);   // brass untouched
    EXPECT_TRUE(scripts::InstanceBindErrors().empty());
}

TEST(ScriptsShellSpawner, SpawnShellsPlacesOneInstancePerShellInSequenceOrder) {
    Loaded l;
    if (!l.ok) GTEST_SKIP() << "run export_runtime_scene.gd (main and instances/shell.tscn)";
    godot::seed(7);
    // compartment_shells already holds its AnimationPlayer (animator_shell compartment).
    const int before = l.compartment->get_child_count();

    l.spawner->SpawnShells(3, 1, 2, false);

    EXPECT_EQ(l.spawner->sequenceArray, (std::vector<std::string>{"live", "blank", "blank"}));
    ASSERT_EQ(l.compartment->get_child_count(), before + 3);
    ASSERT_EQ(l.spawner->spawnedShellObjectArray.size(), 3u);
    for (int i = 0; i < 3; ++i) {
        godot::Node3D* shell = l.spawner->spawnedShellObjectArray[static_cast<std::size_t>(i)];
        ASSERT_NE(shell, nullptr);
        EXPECT_EQ(shell->get_parent(), l.compartment);
        const godot::Vector3 at = shell->get_position();
        EXPECT_NEAR(at.x, l.spawner->shellLocationArray[static_cast<std::size_t>(i)].x, 1e-6f);
        EXPECT_NEAR(at.z, l.spawner->shellLocationArray[static_cast<std::size_t>(i)].z, 1e-6f);
        EXPECT_NEAR(shell->get_rotation_degrees().x, -90.0f, 1e-3f);
        EXPECT_NEAR(shell->get_rotation_degrees().z, 180.0f, 1e-3f);
    }
    EXPECT_NEAR(Body(l.spawner->spawnedShellObjectArray[0])->get_active_surface_albedo(1).r, 1.0f, 1e-5f);
    EXPECT_NEAR(Body(l.spawner->spawnedShellObjectArray[1])->get_active_surface_albedo(1).b, 1.0f, 1e-5f);
    EXPECT_NEAR(Body(l.spawner->spawnedShellObjectArray[2])->get_active_surface_albedo(1).b, 1.0f, 1e-5f);
    ASSERT_NE(l.spawner->ai, nullptr);
    EXPECT_EQ(l.spawner->ai->sequenceArray_knownShell, (std::vector<bool>{false, false, false}));
    EXPECT_EQ(l.tree.template_instances().size(), 6u);   // body + cap per shell
    EXPECT_TRUE(scripts::InstanceBindErrors().empty());
}

TEST(ScriptsShellSpawner, RespawningFreesThePreviousShellsWhenTheFrameFlushes) {
    Loaded l;
    if (!l.ok) GTEST_SKIP() << "run export_runtime_scene.gd (main and instances/shell.tscn)";
    const int before = l.compartment->get_child_count();   // its AnimationPlayer
    l.spawner->SpawnShells(3, 1, 2, false);
    l.spawner->SpawnShells(2, 1, 1, false);
    EXPECT_EQ(l.compartment->get_child_count(), before + 5);   // queue_free waits for the flush
    l.tree.process_frame(1.0 / 60.0);
    EXPECT_EQ(l.compartment->get_child_count(), before + 2);
    EXPECT_EQ(l.spawner->spawnedShellObjectArray.size(), 2u);
}

TEST(ScriptsShellSpawner, ShufflingDrawsFromTheGlobalRng) {
    Loaded l;
    if (!l.ok) GTEST_SKIP() << "run export_runtime_scene.gd (main and instances/shell.tscn)";
    godot::seed(11);
    std::vector<std::string> expected{"live", "live", "blank", "blank"};
    godot::shuffle(expected);

    godot::seed(11);
    l.spawner->SpawnShells(4, 2, 2, true);

    EXPECT_EQ(l.spawner->sequenceArray, expected);
}

TEST(ScriptsShellSpawner, TheLatchSoundAlternates) {
    Loaded l;
    if (!l.ok) GTEST_SKIP() << "run export_runtime_scene.gd (main and instances/shell.tscn)";
    l.spawner->PlayLatchSound();
    EXPECT_EQ(l.spawner->speaker_latchOpen->get_stream(), "shell latch1.ogg");
    l.spawner->PlayLatchSound();
    EXPECT_EQ(l.spawner->speaker_latchOpen->get_stream(), "shell latch2.ogg");
}

TEST(ScriptsDealerIntelligence, HandCrackPlaysARandomCrack) {
    Loaded l;
    if (!l.ok) GTEST_SKIP() << "run export_runtime_scene.gd (main and instances/shell.tscn)";
    scripts::DealerIntelligence* ai = l.spawner->ai;
    ASSERT_NE(ai, nullptr);
    ASSERT_FALSE(ai->soundArray_cracks.empty());
    godot::seed(4);
    const auto index = godot::randi_range(0, static_cast<int64_t>(ai->soundArray_cracks.size()) - 1);
    godot::seed(4);
    ai->Speaker_HandCrack();
    EXPECT_EQ(ai->speaker_handCrack->get_stream(), ai->soundArray_cracks[static_cast<std::size_t>(index)]);
}

// v2.2: ShellEjectManager._ready colours the live/blank materials, and every
// shell draws with those same two resources (ShellClass's exports share them).
TEST(ScriptsShellSpawner, SpawnedShellsTakeTheColoursSetAtReady) {
    Loaded l;
    if (!l.ok) GTEST_SKIP() << "run export_runtime_scene.gd (main and instances/shell.tscn)";
    godot::seed(7);
    l.spawner->SpawnShells(3, 1, 2, false);
    ASSERT_EQ(l.spawner->spawnedShellObjectArray.size(), 3u);
    const godot::Color live = Body(l.spawner->spawnedShellObjectArray[0])->get_active_surface_albedo(1);
    const godot::Color blank = Body(l.spawner->spawnedShellObjectArray[1])->get_active_surface_albedo(1);
    EXPECT_FLOAT_EQ(live.g, scripts::GlobalVariables::default_color_live.g);
    EXPECT_FLOAT_EQ(blank.r, scripts::GlobalVariables::default_color_blank.r);
}
