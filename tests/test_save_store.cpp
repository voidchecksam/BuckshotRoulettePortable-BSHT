// The one persistence layer every translated SaveFileManager / OptionsManager /
// AchievementManager goes through. A save that is missing, empty, truncated or
// from another layout loads as absent and is overwritten cleanly: never a
// crash, never half a map.
#include "game/save_store.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <string>

#include <gtest/gtest.h>

namespace {

using game::SaveStore;
using game::SaveValue;

// A directory unique to the test, under the build tree, created fresh.
std::string FreshDir(const char* name) {
    const std::string dir = std::string(BSHT_BINARY_DIR) + "/save_test_" + name;
    std::remove((dir + "/slot.sav").c_str());
    std::remove((dir + "/slot.tmp").c_str());
    return dir;
}

std::map<std::string, SaveValue> Sample() {
    return {{"has_read_introduction", true},
            {"batch_index", int64_t{2}},
            {"setting_volume", 0.9},
            {"playername", std::string("BALLS")}};
}

TEST(SaveStore, RoundTripsAllFourTypes) {
    const SaveStore store(FreshDir("roundtrip"));
    ASSERT_TRUE(store.Save("slot", Sample()));
    std::map<std::string, SaveValue> loaded;
    ASSERT_TRUE(store.Load("slot", loaded));
    EXPECT_EQ(loaded, Sample());
}

TEST(SaveStore, MissingFileIsFalseAndLeavesOutputEmpty) {
    const SaveStore store(FreshDir("missing"));
    std::map<std::string, SaveValue> loaded;
    EXPECT_FALSE(store.Load("slot", loaded));
    EXPECT_TRUE(loaded.empty());
    EXPECT_FALSE(store.Exists("slot"));
}

TEST(SaveStore, TruncatedFileIsFalseAndNothingHalfLoaded) {
    const std::string dir = FreshDir("truncated");
    const SaveStore store(dir);
    ASSERT_TRUE(store.Save("slot", Sample()));
    std::string bytes;
    {
        std::ifstream in(dir + "/slot.sav", std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), {});
    }
    ASSERT_GT(bytes.size(), 20u);
    {
        std::ofstream out(dir + "/slot.sav", std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size() - 5));  // mid-record
    }
    std::map<std::string, SaveValue> loaded;
    EXPECT_FALSE(store.Load("slot", loaded));
    EXPECT_TRUE(loaded.empty());
}

TEST(SaveStore, EmptyAndWrongMagicAreFalse) {
    const std::string dir = FreshDir("magic");
    const SaveStore store(dir);
    ASSERT_TRUE(store.Save("slot", Sample()));  // creates the directory
    {
        std::ofstream out(dir + "/slot.sav", std::ios::binary | std::ios::trunc);
    }
    std::map<std::string, SaveValue> loaded;
    EXPECT_FALSE(store.Load("slot", loaded));
    {
        std::ofstream out(dir + "/slot.sav", std::ios::binary | std::ios::trunc);
        out << "GDSV\x01\x00\x00\x00 a godot .shell file is not ours";
    }
    EXPECT_FALSE(store.Load("slot", loaded));
    EXPECT_TRUE(loaded.empty());
}

TEST(SaveStore, ACorruptSaveIsOverwrittenCleanly) {
    const std::string dir = FreshDir("overwrite");
    const SaveStore store(dir);
    ASSERT_TRUE(store.Save("slot", Sample()));
    {
        std::ofstream out(dir + "/slot.sav", std::ios::binary | std::ios::trunc);
        out << "BSAV";
    }
    const std::map<std::string, SaveValue> fresh{{"setting_volume", 0.5}};
    ASSERT_TRUE(store.Save("slot", fresh));
    std::map<std::string, SaveValue> loaded;
    ASSERT_TRUE(store.Load("slot", loaded));
    EXPECT_EQ(loaded, fresh);
}

TEST(SaveStore, ClearRemovesTheSave) {
    const SaveStore store(FreshDir("clear"));
    ASSERT_TRUE(store.Save("slot", Sample()));
    EXPECT_TRUE(store.Exists("slot"));
    EXPECT_TRUE(store.Clear("slot"));
    EXPECT_FALSE(store.Exists("slot"));
    EXPECT_TRUE(store.Clear("slot"));  // already gone is still "gone"
}

TEST(SaveStore, AFailedSaveLeavesThePreviousFile) {
    const std::string dir = FreshDir("failed");
    const SaveStore store(dir);
    ASSERT_TRUE(store.Save("slot", Sample()));
    // A directory where the temp file must go makes the write fail.
    const SaveStore blocked(dir + "/slot.sav/not_a_dir");
    EXPECT_FALSE(blocked.Save("slot", {{"x", true}}));
    std::map<std::string, SaveValue> loaded;
    ASSERT_TRUE(store.Load("slot", loaded));
    EXPECT_EQ(loaded, Sample());
}

TEST(SaveStore, AnInterruptedReplaceRecoversFromTheTempFile) {
    // Power lost between removing the old .sav and renaming the .tmp over it
    // (the only window on a filesystem whose rename will not replace): the
    // complete .tmp is the save.
    const std::string dir = FreshDir("interrupted");
    const SaveStore store(dir);
    ASSERT_TRUE(store.Save("slot", Sample()));
    ASSERT_EQ(std::rename((dir + "/slot.sav").c_str(), (dir + "/slot.tmp").c_str()), 0);
    std::map<std::string, SaveValue> loaded;
    ASSERT_TRUE(store.Load("slot", loaded));
    EXPECT_EQ(loaded, Sample());
    EXPECT_TRUE(store.Exists("slot"));
}

}  // namespace

// A save from another format version is refused whole, not half-read.
TEST(SaveStore, AnotherVersionIsRefused) {
    const std::string dir = FreshDir("version");
    const SaveStore store(dir);
    ASSERT_TRUE(store.Save("slot", Sample()));
    {
        std::fstream f(dir + "/slot.sav", std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(4);   // after the magic: the u32 version, little-endian
        const char two[4] = {2, 0, 0, 0};
        f.write(two, 4);
    }
    std::map<std::string, SaveValue> loaded;
    EXPECT_FALSE(store.Load("slot", loaded));
    EXPECT_TRUE(loaded.empty());
}
