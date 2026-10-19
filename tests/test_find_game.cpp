#include "extract/find_game.h"
#include "extract/preflight.h"
#include "extract_test_pack.h"

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

fs::path Root() { return fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_find_game"; }

std::string GameExe(const char* version, int hotfix) {
    testpack::Options o;
    o.prefix = "MZ" + std::string(500, '\0');
    o.trailer = true;
    const std::string gd = std::string("var currentVersion_nr = \"") + version + "\"\nvar currentVersion_hotfix = " +
                           std::to_string(hotfix) + "\n";
    return testpack::Build({{"res://scripts/GlobalVariables.gd", gd}}, o);
}

}  // namespace

TEST(FindGame, TakesTheExeItIsGiven) {
    const auto exe = testpack::Write(Root() / "a" / "Buckshot Roulette.exe", GameExe("v2.2.0", 6));
    const auto r = extract::FindGame(exe.string());
    ASSERT_TRUE(r.game.has_value());
    EXPECT_EQ(r.game->version.hotfix, 6);
}

TEST(FindGame, SkipsFilesWithoutAPackAndLooksOneLevelDown) {
    const fs::path top = Root() / "Buckshot Roulette";
    testpack::Write(top / "BuckshotRouletteHelper.exe", std::string(3000, 'G'));
    testpack::Write(top / "data.win", std::string(3000, 'W'));
    testpack::Write(top / "Buckshot Roulette_windows" / "Buckshot Roulette.exe", GameExe("v2.2.0", 6));
    testpack::Write(top / "Buckshot Roulette_windows" / "deeper" / "other.exe", GameExe("v2.2.0", 6));

    const auto r = extract::FindGame(top.string());

    ASSERT_TRUE(r.game.has_value());
    EXPECT_EQ(fs::path(std::u8string(r.game->path.begin(), r.game->path.end())).filename(), "Buckshot Roulette.exe");
}

// Windows does not care about case, and neither may the folder scan.
TEST(FindGame, FindsAnExeWhateverTheCaseOfItsExtension) {
    const fs::path top = Root() / "upper";
    testpack::Write(top / "BUCKSHOT ROULETTE.EXE", GameExe("v2.2.0", 6));

    const auto r = extract::FindGame(top.string());

    ASSERT_TRUE(r.game.has_value());
}

TEST(FindGame, AnUnsupportedVersionIsNamed) {
    const auto exe = testpack::Write(Root() / "old" / "Buckshot Roulette.exe", GameExe("v1.2.0", 0));
    const auto r = extract::FindGame(exe.string());
    EXPECT_FALSE(r.game.has_value());
    ASSERT_FALSE(r.notes.empty());
    EXPECT_NE(r.notes.back().find("v1.2.0"), std::string::npos);
    EXPECT_NE(r.notes.back().find("v2.2.0 hotfix 6"), std::string::npos);
}

// The tool never writes inside the folder holding the game, nor inside a
// folder the player pointed it at (Steam's install folder sits one level up).
// A console opened in the game's folder, given a bare file name: the folder to
// leave alone is still the game's folder, not "" (which guards nothing).
TEST(FindGame, ARelativeFileNameStillNamesTheGameFolder) {
    const fs::path dir = Root() / "relative";
    testpack::Write(dir / "Buckshot Roulette.exe", GameExe("v2.2.0", 6));
    const fs::path before = fs::current_path();
    fs::current_path(dir);
    const auto r = extract::FindGame("Buckshot Roulette.exe");
    std::vector<fs::path> folders;
    if (r.game) folders = extract::FoldersToLeaveAlone("Buckshot Roulette.exe", *r.game);
    fs::current_path(before);

    ASSERT_TRUE(r.game.has_value());
    ASSERT_EQ(folders.size(), 1u);
    EXPECT_TRUE(folders[0].is_absolute());
    EXPECT_TRUE(extract::IsInside(dir / "bsht-data", folders[0]));
}

TEST(FindGame, TheFoldersToLeaveAloneIncludeTheOneThePlayerGave) {
    const fs::path top = Root() / "Leave Alone";
    const auto exe = testpack::Write(top / "Buckshot Roulette_windows" / "Buckshot Roulette.exe", GameExe("v2.2.0", 6));

    const auto fromFolder = extract::FindGame(top.string());
    ASSERT_TRUE(fromFolder.game.has_value());
    const auto both = extract::FoldersToLeaveAlone(top.string(), *fromFolder.game);
    ASSERT_EQ(both.size(), 2u);
    EXPECT_EQ(both[0].filename(), "Buckshot Roulette_windows");
    EXPECT_EQ(both[1], top);

    const auto fromExe = extract::FindGame(exe.string());
    ASSERT_TRUE(fromExe.game.has_value());
    const auto one = extract::FoldersToLeaveAlone(exe.string(), *fromExe.game);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0].filename(), "Buckshot Roulette_windows");
}
