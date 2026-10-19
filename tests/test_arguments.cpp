#include "extract/arguments.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

extract::Parsed Parse(std::vector<std::string> args) {
    args.insert(args.begin(), "bsht-extract");
    extract::Options defaults;
    defaults.out = "default-out";
    return extract::ParseArguments(args, defaults);
}

TEST(Arguments, AGameAndAnOutputFolderRun) {
    const extract::Parsed p = Parse({"game.exe", "--out", "data", "--keep-work"});
    ASSERT_EQ(p.action, extract::Action::Run) << p.error;
    EXPECT_EQ(p.options.game, "game.exe");
    EXPECT_EQ(p.options.out, "data");
    EXPECT_TRUE(p.options.keepWork);
}

TEST(Arguments, NothingToReadIsAUsageError) {
    EXPECT_EQ(Parse({}).action, extract::Action::Usage);
    EXPECT_EQ(Parse({"--frobnicate"}).action, extract::Action::Usage);
}

TEST(Arguments, UnpackToWritesTheReferenceGameThere) {
    const extract::Parsed p = Parse({"game.exe", "--unpack-to", "external/game-v2.2"});
    ASSERT_EQ(p.action, extract::Action::Run) << p.error;
    EXPECT_EQ(p.options.unpackTo, "external/game-v2.2");
    EXPECT_EQ(p.options.out, "external/game-v2.2");
}

TEST(Arguments, UnpackToNeedsAGameNotAProject) {
    const extract::Parsed p = Parse({"--project", ".", "--unpack-to", "x"});
    EXPECT_EQ(p.action, extract::Action::Usage);
    EXPECT_NE(p.error.find("--unpack-to"), std::string::npos) << p.error;
}

TEST(Arguments, UnpackToAndOutCannotBothBeGiven) {
    EXPECT_EQ(Parse({"game.exe", "--unpack-to", "x", "--out", "y"}).action, extract::Action::Usage);
}

// The release packaging asks the tool which exporter scripts it needs.
TEST(Arguments, ListExportersNeedsNothingElse) {
    EXPECT_EQ(Parse({"--list-exporters"}).action, extract::Action::ListExporters);
}

TEST(Arguments, FingerprintTakesItsFile) {
    const extract::Parsed p = Parse({"--fingerprint", "game.exe"});
    ASSERT_EQ(p.action, extract::Action::Fingerprint);
    EXPECT_EQ(p.fingerprintFile, "game.exe");
}

}  // namespace
