// One scene resident at a time. A script's get_tree().change_scene_to_file(path)
// is recorded and performed by GameApp after the frame, from the files
// FilesForScene names. A change requested while an await is pending must not
// run the old scene's coroutines against the new tree.
#include "app/scene_host.h"

#include "godot/node.h"
#include "godot/scene_tree.h"
#include "seq/task.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace {

TEST(SceneHost, MenuNamesTheMenuSet) {
    const app::SceneFiles f = app::FilesForScene("res://scenes/menu.tscn", "data", "rooms", "rooms");
    EXPECT_EQ(f.name, "menu");
    EXPECT_EQ(f.room, "rooms/menu.room");
    EXPECT_EQ(f.runtime, "data/runtime_menu.json");
    EXPECT_EQ(f.animations, "data/animations_menu.json");
    EXPECT_EQ(f.ui, "data/ui_layout_menu.json");
    EXPECT_EQ(f.uiPixels, "data/ui_textures_menu.bin");
    EXPECT_EQ(f.labels, "");  // menu.tscn has no Label3D
    EXPECT_EQ(f.logo, "rooms/menu_logo.rgba");  // the port's title logo (tools/bake_menu_logo.py)
}

TEST(SceneHost, MainNamesTheFilesMainAlwaysHad) {
    const app::SceneFiles f = app::FilesForScene("res://scenes/main.tscn", "data", "rooms", "rooms");
    EXPECT_EQ(f.name, "main");
    EXPECT_EQ(f.room, "rooms/full_world.room");
    EXPECT_EQ(f.runtime, "data/runtime_main.json");
    EXPECT_EQ(f.animations, "data/animations.json");
    EXPECT_EQ(f.ui, "data/ui_layout.json");
    EXPECT_EQ(f.uiPixels, "data/ui_textures.bin");
    EXPECT_EQ(f.labels, "data/labels3d.json");
    EXPECT_EQ(f.logo, "");  // the logo is the menu's alone
}

TEST(SceneHost, DeathReloadsMain) {
    // TempDeathScreen._ready (isDeathScreen) changes straight back to main.
    EXPECT_EQ(app::FilesForScene("res://scenes/death.tscn", "d", "r", "r").name, "main");
}

TEST(SceneHost, AnUnknownSceneNamesNothing) {
    const app::SceneFiles f = app::FilesForScene("res://scenes/win.tscn", "d", "r", "r");
    EXPECT_TRUE(f.name.empty());
    EXPECT_TRUE(f.room.empty());
}

// A true death loads heaven.tscn (DeathManager.gd:154).
TEST(SceneHost, HeavenNamesItsOwnFiles) {
    const app::SceneFiles f = app::FilesForScene("res://scenes/heaven.tscn", "d", "r", "r");
    EXPECT_EQ(f.name, "heaven");
    EXPECT_EQ(f.room, "r/heaven.room");
    EXPECT_EQ(f.runtime, "d/runtime_heaven.json");
    EXPECT_EQ(f.animations, "d/animations_heaven.json");
    EXPECT_EQ(f.ui, "d/ui_layout_heaven.json");
    EXPECT_EQ(f.uiPixels, "d/ui_textures_heaven.bin");
    EXPECT_TRUE(f.labels.empty()) << "heaven has no Label3D";
}

seq::Task SetAfter(godot::SceneTree* tree, bool* flag) {
    co_await tree->create_timer(0.5);
    *flag = true;
}

TEST(SceneHost, AChangeIsTakenAfterTheFrameAndTheOldTimersDieWithTheOldTree) {
    auto oldTree = std::make_unique<godot::SceneTree>();
    oldTree->add_node(std::make_unique<godot::Node>("menu"), nullptr);
    bool fired = false;
    oldTree->spawn(SetAfter(oldTree.get(), &fired));
    oldTree->change_scene_to_file("res://scenes/main.tscn");
    oldTree->process_frame(0.1);
    const std::string next = oldTree->take_scene_change();
    EXPECT_EQ(next, "res://scenes/main.tscn");
    EXPECT_TRUE(oldTree->take_scene_change().empty());  // taken once

    // The host replaces the tree, as GameApp::ChangeScene does.
    oldTree = std::make_unique<godot::SceneTree>();
    oldTree->add_node(std::make_unique<godot::Node>("main"), nullptr);
    for (int i = 0; i < 60; ++i) oldTree->process_frame(1.0 / 60.0);
    EXPECT_FALSE(fired);
}

}  // namespace

namespace {

// Only a player's boot reads and writes the save directory. A diagnostic that
// skips the menu, and a replayed scenario even when it shows the menu, run
// from no saves and leave none: a frames_menu_ja grab once switched the
// desktop build's language to Japanese for the next real boot.
TEST(SceneHost, OnlyAPlayersBootUsesTheSaveDirectory) {
    EXPECT_TRUE(app::UsesSaveStore(/*skipMenu=*/false, /*scenario=*/false));
    EXPECT_FALSE(app::UsesSaveStore(/*skipMenu=*/true, /*scenario=*/false));
    EXPECT_FALSE(app::UsesSaveStore(/*skipMenu=*/false, /*scenario=*/true));
}

}  // namespace

namespace {

// ChangeScene destroyed the old tree before it knew the new scene's
// files could be read, leaving GameApp pointing into freed nodes. The files
// are checked first now, and a scene whose files are not all there is refused.
TEST(SceneHost, ASceneIsPresentOnlyWhenEveryFileItNamesCanBeRead) {
    const std::string dir = std::string(BSHT_BINARY_DIR) + "/scene_present_test";
    std::filesystem::create_directories(dir);
    for (const char* f : {"r.room", "rt.json", "a.json", "ui.json", "px.bin"}) {
        std::ofstream(dir + "/" + f) << "x";
    }
    app::SceneFiles s;
    s.name = "menu";
    s.room = dir + "/r.room";
    s.runtime = dir + "/rt.json";
    s.animations = dir + "/a.json";
    s.ui = dir + "/ui.json";
    s.uiPixels = dir + "/px.bin";
    EXPECT_TRUE(app::SceneFilesPresent(s));   // labels empty: the menu has none
    s.runtime = dir + "/missing_runtime.json";
    EXPECT_FALSE(app::SceneFilesPresent(s));
    s.runtime = dir + "/rt.json";
    s.room = dir + "/missing.room";
    EXPECT_FALSE(app::SceneFilesPresent(s));
}

}  // namespace

namespace {

// Only a player's boot (the menu's START, a death reload) plays the restroom
// intro; diagnostics and scenarios start at the table.
TEST(SceneHost, OnlyAPlayersBootPlaysTheIntro) {
    EXPECT_TRUE(app::PlaysIntro(/*skipMenu=*/false, /*scenario=*/false));
    EXPECT_FALSE(app::PlaysIntro(/*skipMenu=*/true, /*scenario=*/false));
    EXPECT_FALSE(app::PlaysIntro(/*skipMenu=*/false, /*scenario=*/true));
}

}  // namespace
