#include "extract/godot_run.h"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace {

fs::path Root() { return fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_godot_run"; }

std::string Slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

}  // namespace

TEST(GodotRun, BuildsTheHeadlessCommandLine) {
    const auto args = extract::GodotArgs(fs::path("W") / "game", "res://tools/godot/export_ui.gd",
                                         {"scenes/menu.tscn", "menu", "--out=X"});
    const std::vector<std::string> want = {"--headless", "--fixed-fps", "60", "--path",
                                           (fs::path("W") / "game").string(), "--script",
                                           "res://tools/godot/export_ui.gd", "--", "scenes/menu.tscn", "menu",
                                           "--out=X"};
    EXPECT_EQ(args, want);
}

TEST(GodotRun, MovesUserDataPerOs) {
    EXPECT_EQ(extract::UserDataVariable(extract::HostOs::Windows, "U"), "APPDATA=U");
    EXPECT_EQ(extract::UserDataVariable(extract::HostOs::Linux, "U"), "XDG_DATA_HOME=U");
    EXPECT_EQ(extract::UserDataVariable(extract::HostOs::MacOs, "U"), "HOME=U");
}

// reproc appends extra variables after the parent's and both Windows and glibc
// read the first, so a plain append would leave APPDATA at the player's own.
TEST(GodotRun, OurVariablesReplaceTheParents) {
    const std::vector<std::string> parent = {"=C:=C:\\x", "Path=C:\\bin", "AppData=C:\\real", "HOME=/h"};

    const auto windows = extract::MergeEnvironment(parent, {"APPDATA=W"}, true);
    const auto posix = extract::MergeEnvironment(parent, {"HOME=W"}, false);

    EXPECT_EQ(windows, (std::vector<std::string>{"=C:=C:\\x", "Path=C:\\bin", "HOME=/h", "APPDATA=W"}));
    EXPECT_EQ(posix, (std::vector<std::string>{"=C:=C:\\x", "Path=C:\\bin", "AppData=C:\\real", "HOME=W"}));
}

TEST(GodotRun, PassesArgumentsWithSpacesAndNonAscii) {
    const fs::path work = Root() / fs::path(u8"wörk dir");
    fs::remove_all(work);
    fs::create_directories(work);
    extract::ChildRun run;
    run.program = BSHT_FAKE_GODOT;
    run.args = {"--path", reinterpret_cast<const char*>(u8"C:/Spiele/Müller/Buckshot Roulette"), "two words"};
    run.workingDir = work;
    run.extraEnv = {extract::UserDataVariable(extract::ThisOs(), work / "user")};
    run.log = work / "child.log";

    const extract::ChildResult r = extract::RunChild(run);

    ASSERT_TRUE(r.started) << r.error;
    EXPECT_EQ(r.exitCode, 0);
    const std::string got = Slurp(work / "fake_godot_out.txt");
    EXPECT_NE(got.find(reinterpret_cast<const char*>(u8"arg C:/Spiele/Müller/Buckshot Roulette\n")), std::string::npos)
        << got;
    EXPECT_NE(got.find("arg two words\n"), std::string::npos);
    EXPECT_NE(got.find(reinterpret_cast<const char*>(u8"cwd wörk dir")), std::string::npos);
    const std::u8string user = (work / "user").u8string();
    EXPECT_NE(got.find("=" + std::string(user.begin(), user.end()) + "\n"), std::string::npos) << got;
    EXPECT_NE(Slurp(run.log).find("fake godot ran"), std::string::npos);
}

TEST(GodotRun, ReportsTheChildsExitCode) {
    const fs::path dir = Root() / "ReportsTheChildsExitCode";
    fs::create_directories(dir);
    extract::ChildRun run;
    run.program = BSHT_FAKE_GODOT;
    run.args = {"fail"};
    run.workingDir = dir;
    run.log = dir / "fail.log";
    EXPECT_EQ(extract::RunChild(run).exitCode, 3);
}

TEST(GodotRun, AChildThatHangsIsStoppedAtItsTimeout) {
    const fs::path dir = Root() / "AChildThatHangsIsStoppedAtItsTimeout";
    fs::create_directories(dir);
    extract::ChildRun run;
    run.program = BSHT_FAKE_GODOT;
    run.args = {"sleep"};
    run.workingDir = dir;
    run.log = dir / "sleep.log";
    run.timeout = std::chrono::milliseconds(500);
    const auto start = std::chrono::steady_clock::now();

    const extract::ChildResult r = extract::RunChild(run);

    EXPECT_TRUE(r.timedOut);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
}

TEST(GodotRun, AStopRequestEndsTheChild) {
    const fs::path dir = Root() / "AStopRequestEndsTheChild";
    fs::create_directories(dir);
    extract::ChildRun run;
    run.program = BSHT_FAKE_GODOT;
    run.args = {"sleep"};
    run.workingDir = dir;
    run.log = dir / "stop.log";
    std::thread stopper([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        extract::RequestStop();
    });

    const extract::ChildResult r = extract::RunChild(run);
    stopper.join();
    extract::ClearStop();

    EXPECT_TRUE(r.stopped);
}

TEST(GodotRun, VersionIsTheFirstLineTheBinaryPrints) {
    const fs::path dir = Root() / "VersionIsTheFirstLineTheBinaryPrints";
    fs::create_directories(dir);
    EXPECT_EQ(extract::GodotVersion(BSHT_FAKE_GODOT, dir), "fake godot ran");
    EXPECT_FALSE(fs::exists(dir / ".godot-version.log"));
    EXPECT_EQ(extract::GodotVersion(dir / "no-such-godot.exe", dir), "");
}
