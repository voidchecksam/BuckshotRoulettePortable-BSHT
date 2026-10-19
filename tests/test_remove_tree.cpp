#include "extract/remove_tree.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <fstream>

#if defined(_WIN32)
#include <share.h>
#include <cstdio>
#endif

namespace fs = std::filesystem;

namespace {

fs::path Dir(const char* test) { return fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_remove_tree" / test; }

void Touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << "x";
}

}  // namespace

TEST(RemoveTree, RemovesFoldersFilesAndReadOnlyFiles) {
    const fs::path root = Dir("RemovesFoldersFilesAndReadOnlyFiles");
    Touch(root / "a" / "b" / "c.txt");
    Touch(root / "ro.txt");
    fs::permissions(root / "ro.txt", fs::perms::owner_write, fs::perm_options::remove);
    std::string error;

    EXPECT_TRUE(extract::RemoveTree(root, error)) << error;

    EXPECT_FALSE(fs::exists(root));
}

TEST(RemoveTree, NothingThereIsFine) {
    std::string error;
    EXPECT_TRUE(extract::RemoveTree(Dir("NothingThereIsFine") / "missing", error)) << error;
}

#if defined(_WIN32)
// A file another program keeps open is retried, then reported by name.
TEST(RemoveTree, AFileHeldOpenIsNamedAfterAFewTries) {
    const fs::path root = Dir("AFileHeldOpenIsNamedAfterAFewTries");
    Touch(root / "held.txt");
    FILE* held = _wfsopen((root / "held.txt").c_str(), L"rb", _SH_DENYRW);
    ASSERT_NE(held, nullptr);
    std::string error;
    const auto start = std::chrono::steady_clock::now();

    const bool removed = extract::RemoveTree(root, error);

    std::fclose(held);
    EXPECT_FALSE(removed);
    EXPECT_NE(error.find("held.txt"), std::string::npos) << error;
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
}
#endif

#if defined(_WIN32)
// A junction is a link: removing the tree removes the link and leaves what it
// points at alone (a player could link <out>/game to another drive).
TEST(RemoveTree, RemovesAJunctionNotWhatItPointsAt) {
    const fs::path root = Dir("RemovesAJunctionNotWhatItPointsAt");
    Touch(root / "outside" / "precious.txt");
    fs::create_directories(root / "work");
    const std::string cmd = "cmd /c mklink /J \"" + (root / "work" / "link").make_preferred().string() + "\" \"" +
                            (root / "outside").make_preferred().string() + "\" >nul";
    ASSERT_EQ(std::system(cmd.c_str()), 0);
    ASSERT_TRUE(fs::exists(root / "work" / "link" / "precious.txt"));
    std::string error;

    ASSERT_TRUE(extract::RemoveTree(root / "work", error)) << error;

    EXPECT_FALSE(fs::exists(root / "work"));
    EXPECT_TRUE(fs::exists(root / "outside" / "precious.txt"));
}
#endif
