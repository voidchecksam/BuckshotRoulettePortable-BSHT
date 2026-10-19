// Where the port reads its data: the dev assets/ folders, or bsht-extract's
// flat game folder (the Vita's ux0:data/bsht/game, the desktop's --data).
#include "app/data_layout.h"

#include "app/scene_host.h"

#include <gtest/gtest.h>

namespace {

TEST(DataLayout, DevAssetsKeepTheRepoFolders) {
    const app::DataLayout l = app::DevAssetsLayout();
    EXPECT_EQ(l.data, "assets/extracted");
    EXPECT_EQ(l.rooms, "assets/baked");
    EXPECT_EQ(l.port, "assets/baked");
    EXPECT_EQ(l.sfxBank, "assets/baked/sfx.bank");
    EXPECT_EQ(l.menuMusic, "assets/extracted/audio/music/music main_gate modded.ogg");
    EXPECT_EQ(l.roomMusic, "assets/extracted/audio/music_room vol2.ogg");
    EXPECT_EQ(l.musicDir, "assets/extracted/audio/music");
    EXPECT_EQ(l.achievements, "tools/achievements_en.json");   // the port's text, tracked
    EXPECT_FALSE(l.spaceFreeMusic);
    EXPECT_FALSE(l.gameFolder);
}

TEST(DataLayout, GameFolderIsFlat) {
    const app::DataLayout l = app::GameFolderLayout("G", "P");
    EXPECT_EQ(l.data, "G");
    EXPECT_EQ(l.rooms, "G");
    EXPECT_EQ(l.port, "P");
    EXPECT_EQ(l.sfxBank, "G/sfx.bank");
    EXPECT_EQ(l.menuMusic, "G/music_main_gate_modded.ogg");   // MenuManager's stream, flat
    EXPECT_EQ(l.roomMusic, "G/music_room.ogg");
    EXPECT_EQ(l.musicDir, "G");
    EXPECT_EQ(l.achievements, "P/achievements_en.json");   // a port file, beside the logo
    EXPECT_TRUE(l.spaceFreeMusic);   // bsht-extract writes the music without spaces
    EXPECT_TRUE(l.gameFolder);
}

// The desktop's mouse cursors: the export folder in the dev tree, the game
// folder's cursors/ from bsht-extract.
TEST(DataLayout, CursorsLiveBesideTheData) {
    EXPECT_EQ(app::DevAssetsLayout().cursors, "assets/extracted/cursors");
    EXPECT_EQ(app::GameFolderLayout("G", "P").cursors, "G/cursors");
}

TEST(DataLayout, GameFolderDropsATrailingSlash) {
    EXPECT_EQ(app::GameFolderLayout("G/", "P").sfxBank, "G/sfx.bank");
}

TEST(DataLayout, DescribeNamesTheFolders) {
    const std::string d = app::DescribeLayout(app::GameFolderLayout("ux0:data/bsht/game", "app0:data"));
    EXPECT_NE(d.find("ux0:data/bsht/game"), std::string::npos) << d;
    EXPECT_NE(d.find("app0:data"), std::string::npos) << d;
}

TEST(DataLayout, TheMenuLogoComesFromThePortsOwnFolder) {
    const app::SceneFiles f = app::FilesForScene("res://scenes/menu.tscn", "d", "r", "p");
    EXPECT_EQ(f.logo, "p/menu_logo.rgba");
    EXPECT_EQ(f.room, "r/menu.room");
}

}  // namespace
