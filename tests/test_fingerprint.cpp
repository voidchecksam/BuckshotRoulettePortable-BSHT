#include "extract/fingerprint.h"
#include "extract/pack_reader.h"
#include "extract_test_pack.h"

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

fs::path Dir() { return fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_fingerprint"; }

std::string Of(const std::vector<testpack::File>& files, const char* name) {
    const auto path = testpack::Write(Dir() / name, testpack::Build(files));
    extract::PackInfo info;
    EXPECT_EQ(extract::ReadPack(path.string(), info), extract::PackError::None);
    std::string error;
    const std::string fp = extract::PackFingerprint(path.string(), info, error);
    EXPECT_TRUE(error.empty()) << error;
    return fp;
}

}  // namespace

TEST(Fingerprint, TheSameFilesGiveTheSameFingerprint) {
    const std::vector<testpack::File> files = {{"res://a.gd", "x"}, {"res://b.scn", "yy"}};
    EXPECT_EQ(Of(files, "one.pck"), Of(files, "two.pck"));
    EXPECT_EQ(Of(files, "one.pck").size(), 64u);
}

TEST(Fingerprint, OneChangedByteOrNameChangesIt) {
    const std::string base = Of({{"res://a.gd", "x"}, {"res://b.scn", "yy"}}, "base.pck");
    EXPECT_NE(base, Of({{"res://a.gd", "z"}, {"res://b.scn", "yy"}}, "byte.pck"));
    EXPECT_NE(base, Of({{"res://c.gd", "x"}, {"res://b.scn", "yy"}}, "name.pck"));
    EXPECT_NE(base, Of({{"res://a.gd", "x"}}, "fewer.pck"));
}

TEST(Fingerprint, OnlyKnownBuildsAreAccepted) {
    EXPECT_EQ(extract::FindKnownBuild(Of({}, "empty.pck")), nullptr);
}
