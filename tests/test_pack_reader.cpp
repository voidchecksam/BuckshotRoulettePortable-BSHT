#include "extract/pack_reader.h"
#include "extract_test_pack.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace fs = std::filesystem;
using extract::PackError;

namespace {

fs::path Dir() { return fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_pack_reader"; }

const std::vector<testpack::File> kFiles = {
    {"res://scripts/GlobalVariables.gd", "var currentVersion_nr = \"v2.2.0\"\n"},
    {"res://audio/a.ogg.import", "[remap]\n"},
};

}  // namespace

TEST(PackReader, ReadsABarePack) {
    const auto path = testpack::Write(Dir() / "bare.pck", testpack::Build(kFiles));
    extract::PackInfo info;

    ASSERT_EQ(extract::ReadPack(path.string(), info), PackError::None);

    EXPECT_EQ(info.format, 2u);
    EXPECT_EQ(info.minor, 1u);
    ASSERT_EQ(info.entries.size(), 2u);
    EXPECT_EQ(info.entries[0].path, "res://scripts/GlobalVariables.gd");
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(extract::ReadEntry(path.string(), info.entries[1], bytes));
    EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "[remap]\n");
}

TEST(PackReader, FindsAPackAppendedToAnExecutable) {
    testpack::Options o;
    o.prefix = std::string(1000, 'M');
    o.trailer = true;
    const auto path = testpack::Write(Dir() / "game.exe", testpack::Build(kFiles, o));
    extract::PackInfo info;

    ASSERT_EQ(extract::ReadPack(path.string(), info), PackError::None);

    const extract::PackEntry* gv = extract::FindEntry(info, "res://scripts/GlobalVariables.gd");
    ASSERT_NE(gv, nullptr);
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(extract::ReadEntry(path.string(), *gv, bytes));
    EXPECT_EQ(std::string(bytes.begin(), bytes.end()).rfind("var currentVersion_nr", 0), 0u);
}

TEST(PackReader, SaysWhenAFileHasNoPack) {
    const auto path = testpack::Write(Dir() / "data.win", std::string(4096, 'x'));
    extract::PackInfo info;
    EXPECT_EQ(extract::ReadPack(path.string(), info), PackError::NoPack);
    EXPECT_EQ(extract::ReadPack((Dir() / "missing.exe").string(), info), PackError::CannotOpen);
}

TEST(PackReader, RefusesOtherFormatsAndNewerEngines) {
    extract::PackInfo info;
    testpack::Options v3;
    v3.format = 3;
    EXPECT_EQ(extract::ReadPack(testpack::Write(Dir() / "v3.pck", testpack::Build(kFiles, v3)).string(), info),
              PackError::UnsupportedFormat);
    testpack::Options godot42;
    godot42.minor = 2;
    EXPECT_EQ(extract::ReadPack(testpack::Write(Dir() / "g42.pck", testpack::Build(kFiles, godot42)).string(), info),
              PackError::NewerEngine);
}

TEST(PackReader, RefusesEncryptedPacks) {
    extract::PackInfo info;
    testpack::Options dir;
    dir.packFlags = 1;
    EXPECT_EQ(extract::ReadPack(testpack::Write(Dir() / "encdir.pck", testpack::Build(kFiles, dir)).string(), info),
              PackError::Encrypted);
    std::vector<testpack::File> files = kFiles;
    files[1].flags = 1;
    EXPECT_EQ(extract::ReadPack(testpack::Write(Dir() / "encfile.pck", testpack::Build(files)).string(), info),
              PackError::Encrypted);
}

TEST(PackReader, RefusesATruncatedPack) {
    std::string bytes = testpack::Build(kFiles);
    bytes.resize(bytes.size() - 10);   // the last file's data runs past the end
    extract::PackInfo info;
    EXPECT_EQ(extract::ReadPack(testpack::Write(Dir() / "cut.pck", bytes).string(), info), PackError::Truncated);
    EXPECT_EQ(extract::ReadPack(testpack::Write(Dir() / "head.pck", bytes.substr(0, 40)).string(), info),
              PackError::Truncated);
}

// A damaged or hostile directory must not make the reader allocate or seek
// past the file.
TEST(PackReader, RefusesADirectoryThatClaimsMoreFilesThanFit) {
    testpack::Options o;
    o.claimedCount = 0xFFFFFFF0u;
    extract::PackInfo info;
    EXPECT_EQ(extract::ReadPack(testpack::Write(Dir() / "many.pck", testpack::Build(kFiles, o)).string(), info),
              PackError::Truncated);
    EXPECT_TRUE(info.entries.empty());
}

// Read only. Skips without BSHT_GAME_EXE: a real copy, when it names a v2.2.0
// hotfix 6 Buckshot Roulette.exe.
TEST(PackReader, ReadsARealGameCopy) {
    const char* exe = std::getenv("BSHT_GAME_EXE");
    if (!exe || !fs::exists(exe)) GTEST_SKIP() << "set BSHT_GAME_EXE to a v2.2.0 hotfix 6 exe";
    extract::PackInfo info;
    ASSERT_EQ(extract::ReadPack(exe, info), PackError::None);
    EXPECT_EQ(info.entries.size(), 3572u);
    EXPECT_EQ(info.patch, 1u);
    EXPECT_NE(extract::FindEntry(info, "res://scripts/GlobalVariables.gd"), nullptr);
}
