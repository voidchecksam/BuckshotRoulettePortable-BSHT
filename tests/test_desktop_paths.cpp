#include "app/desktop_paths.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

// The release zip: bsht_desktop.exe with data/ beside it, and the player's
// game/ copied in next to them. The dev tree has neither and reads assets/.

TEST(DesktopPaths, TheReleaseZipReadsTheGameFolderBesideTheExe) {
    const app::DesktopPaths p = app::ChooseDesktopPaths("D:/games/bsht-desktop", "", true);
    EXPECT_TRUE(p.layout.gameFolder);
    EXPECT_EQ(p.layout.data, "D:/games/bsht-desktop/game");
    EXPECT_EQ(p.layout.port, "D:/games/bsht-desktop/data");
    EXPECT_EQ(p.layout.achievements, "D:/games/bsht-desktop/data/achievements_en.json");
    EXPECT_EQ(p.saveDir, "D:/games/bsht-desktop/save");
}

// Before game/ is copied in, the data check still looks there, so the screen
// names the folder the player has to fill.
TEST(DesktopPaths, TheReleaseZipWithoutAGameFolderStillChecksThatFolder) {
    const app::DesktopPaths p = app::ChooseDesktopPaths("D:/games/bsht-desktop", "", true);
    EXPECT_TRUE(p.layout.gameFolder);
    EXPECT_EQ(p.layout.data, "D:/games/bsht-desktop/game");
}

TEST(DesktopPaths, DataArgumentInTheReleaseZipKeepsThePortFilesBesideTheExe) {
    const app::DesktopPaths p = app::ChooseDesktopPaths("D:/games/bsht-desktop", "E:/elsewhere/game", true);
    EXPECT_TRUE(p.layout.gameFolder);
    EXPECT_EQ(p.layout.data, "E:/elsewhere/game");
    EXPECT_EQ(p.layout.port, "D:/games/bsht-desktop/data");
    EXPECT_EQ(p.layout.achievements, "D:/games/bsht-desktop/data/achievements_en.json");
    EXPECT_EQ(p.saveDir, "D:/games/bsht-desktop/save");
}

TEST(DesktopPaths, TheDevTreeKeepsAssetsAndTools) {
    const app::DesktopPaths dev = app::ChooseDesktopPaths("E:/repo/build/src", "", false);
    EXPECT_FALSE(dev.layout.gameFolder);
    EXPECT_EQ(dev.layout.data, "assets/extracted");
    EXPECT_EQ(dev.saveDir, "save");

    const app::DesktopPaths withData = app::ChooseDesktopPaths("E:/repo/build/src", "bsht-data/game", false);
    EXPECT_TRUE(withData.layout.gameFolder);
    EXPECT_EQ(withData.layout.data, "bsht-data/game");
    EXPECT_EQ(withData.layout.port, "assets/baked");
    EXPECT_EQ(withData.layout.achievements, "tools/achievements_en.json");
    EXPECT_EQ(withData.saveDir, "save");
}

// The zip is recognised by its own files, in a folder of any name.
TEST(DesktopPaths, TheReleaseLayoutIsTheMenuLogoInData) {
    namespace fs = std::filesystem;
    const auto utf8 = [](const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); };
    const std::string dir = (fs::temp_directory_path() / "bsht_desktop_paths_test").string() + "/Zo\xc3\xab K";
    fs::remove_all(fs::temp_directory_path() / "bsht_desktop_paths_test");
    fs::create_directories(utf8(dir + "/data"));
    EXPECT_FALSE(app::IsReleaseLayout(dir));
    std::ofstream(utf8(dir + "/data/menu_logo.rgba")) << "x";
    EXPECT_TRUE(app::IsReleaseLayout(dir));
    fs::remove_all(fs::temp_directory_path() / "bsht_desktop_paths_test");
}

TEST(DesktopPaths, AFolderWithSpacesAndAccentsIsKeptAsIs) {
    const std::string dir = "C:/Users/Zo\xc3\xab K/Downloads/bsht-desktop";
    const app::DesktopPaths p = app::ChooseDesktopPaths(dir, "", true);
    EXPECT_EQ(p.layout.data, dir + "/game");
    EXPECT_EQ(p.layout.sfxBank, dir + "/game/sfx.bank");
    EXPECT_EQ(p.saveDir, dir + "/save");
}
