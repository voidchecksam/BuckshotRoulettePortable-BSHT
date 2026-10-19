#include "extract/preflight.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace {

extract::PreflightFacts Good() {
    extract::PreflightFacts f;
    f.out = fs::path("C:/bsht-data");
    f.gameFolder = fs::path("D:/Steam/common/Buckshot Roulette/Buckshot Roulette_windows");
    f.freeBytes = 10ull << 30;
    f.godotVersion = "4.1.4.stable.official.4ce3e68e1";
    return f;
}

bool Says(const std::vector<std::string>& problems, const char* words) {
    for (const auto& p : problems) {
        if (p.find(words) != std::string::npos) return true;
    }
    return false;
}

// One folder per test: ctest runs them as parallel processes.
fs::path Dir(const char* test) { return fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_preflight" / test; }

}  // namespace

TEST(Preflight, AGoodSetupHasNoProblems) { EXPECT_TRUE(extract::PreflightProblems(Good()).empty()); }

TEST(Preflight, NeverWritesInsideTheGameFolder) {
    auto f = Good();
    f.out = f.gameFolder / "bsht-data";
    EXPECT_TRUE(Says(extract::PreflightProblems(f), "outside the game's folder"));
    f.out = f.gameFolder;
    EXPECT_TRUE(Says(extract::PreflightProblems(f), "outside the game's folder"));
    f.out = fs::path("D:/Steam/common/Buckshot Roulette/Buckshot Roulette_windows2");
    EXPECT_FALSE(Says(extract::PreflightProblems(f), "outside the game's folder"));
}

#if defined(_WIN32)
// Windows paths ignore case, so "d:\steam\..." is the same folder.
TEST(Preflight, TheGameFolderCheckIgnoresCaseOnWindows) {
    auto f = Good();
    f.out = fs::path("d:/steam/COMMON/buckshot roulette/Buckshot Roulette_Windows/x");
    EXPECT_TRUE(Says(extract::PreflightProblems(f), "outside the game's folder"));
}
#endif

TEST(Preflight, NeedsRoomOnTheDisk) {
    auto f = Good();
    f.freeBytes = 1ull << 30;
    EXPECT_TRUE(Says(extract::PreflightProblems(f), "GB free"));
}

TEST(Preflight, AWindowsPathThatWouldBeTooLongIsCaughtFirst) {
    auto f = Good();
    f.out = fs::path("C:/" + std::string(200, 'a'));
    EXPECT_TRUE(Says(extract::PreflightProblems(f), "shorter"));
    f.os = extract::HostOs::Linux;
    EXPECT_FALSE(Says(extract::PreflightProblems(f), "shorter"));
}

// The unpacked game is written with the long-path prefix and Godot adds it
// itself, so only the files the tool reads under .work/out limit the output
// folder: up to 155 characters past ".work/" in v1.2 and v2.2.
TEST(Preflight, OnlyWhatTheToolReadsLimitsTheOutputPath) {
    auto f = Good();
    f.out = fs::path("C:/" + std::string(87, 'a'));   // 90 characters
    EXPECT_FALSE(Says(extract::PreflightProblems(f), "shorter"));
    f.out = fs::path("C:/" + std::string(97, 'a'));   // 100
    EXPECT_TRUE(Says(extract::PreflightProblems(f), "shorter"));
#if defined(_WIN32)
    // 90 characters as Windows counts them, 177 bytes of UTF-8.
    f.out = fs::path(L"C:/" + std::wstring(87, L'\u00fc'));
    EXPECT_FALSE(Says(extract::PreflightProblems(f), "shorter"));
#endif
}

TEST(Preflight, NeedsGodot414AndEveryExporter) {
    auto f = Good();
    f.godotVersion = "4.2.1.stable.official";
    f.missingExporters = {"export_ui.gd"};
    const auto p = extract::PreflightProblems(f);
    EXPECT_TRUE(Says(p, "Godot 4.1.4"));
    EXPECT_TRUE(Says(p, "export_ui.gd"));
}

TEST(Preflight, LeavesFoldersItDidNotMakeAlone) {
    auto f = Good();
    f.foreignGameFolder = true;
    f.foreignWorkFolder = true;
    const auto p = extract::PreflightProblems(f);
    EXPECT_TRUE(Says(p, "game"));
    EXPECT_TRUE(Says(p, ".work"));
}

TEST(Preflight, KnowsItsOwnFolders) {
    const fs::path dir = Dir("KnowsItsOwnFolders");
    fs::remove_all(dir);
    fs::create_directories(dir / "game");
    std::ofstream(dir / "game" / "manifest.json") << "{\n \"format\": 1,\n \"tool\": \"bsht-extract 1.0\"\n}\n";
    fs::create_directories(dir / "work");
    std::ofstream(dir / "work" / ".bsht-extract") << "";
    fs::create_directories(dir / "other");
    std::ofstream(dir / "other" / "save.dat") << "x";
    EXPECT_TRUE(extract::MadeByUs(dir / "game"));
    EXPECT_TRUE(extract::MadeByUs(dir / "work"));
    EXPECT_FALSE(extract::MadeByUs(dir / "other"));
    EXPECT_TRUE(extract::MadeByUs(dir / "missing"));   // nothing there: nothing to protect
}

TEST(Preflight, OnlyOneRunPerOutputFolder) {
    const fs::path dir = Dir("OnlyOneRunPerOutputFolder");
    fs::create_directories(dir);
    {
        extract::OutputLock first(dir);
        extract::OutputLock second(dir);
        EXPECT_TRUE(first.Held());
        EXPECT_FALSE(second.Held());
        EXPECT_TRUE(second.Busy());
    }
    extract::OutputLock again(dir);
    EXPECT_TRUE(again.Held());
}

// A run that was killed (console closed, Task Manager) leaves its lock file
// behind; the lock is the open file, not the file, so it does not block.
TEST(Preflight, ALockFileLeftByAKilledRunDoesNotBlock) {
    const fs::path dir = Dir("ALockFileLeftByAKilledRunDoesNotBlock");
    fs::create_directories(dir);
    std::ofstream(dir / ".bsht-extract.lock") << "left behind";

    extract::OutputLock lock(dir);

    EXPECT_TRUE(lock.Held()) << lock.Error();
    EXPECT_FALSE(lock.Busy());
}

TEST(Preflight, ArgumentsLoseStrayQuotesAndSpaces) {
    // cmd.exe turns "D:\Games\Buckshot Roulette\" into D:\Games\Buckshot Roulette" (the \" escapes).
    EXPECT_EQ(extract::CleanArgument("D:\\Games\\Buckshot Roulette\""), "D:\\Games\\Buckshot Roulette");
    EXPECT_EQ(extract::CleanArgument("  \"C:\\x.exe\"  "), "C:\\x.exe");
}

#if defined(_WIN32)
// The game folder reached another way still counts: through a junction, or
// spelled with other case in letters beyond ASCII.
TEST(Preflight, TheGameFolderCheckSeesThroughJunctionsAndCase) {
    const fs::path base = Dir("SeesThroughJunctionsAndCase");
    const fs::path game = base / fs::path(u8"Müller game");
    fs::create_directories(game);
    const std::wstring cmd = L"cmd /c mklink /J \"" + (base / "alias").make_preferred().wstring() + L"\" \"" +
                             fs::path(game).make_preferred().wstring() + L"\" >nul";
    if (!fs::exists(base / "alias")) ASSERT_EQ(_wsystem(cmd.c_str()), 0);

    EXPECT_TRUE(extract::IsInside(base / "alias" / "bsht-data", game));
    EXPECT_TRUE(extract::IsInside(base / fs::path(u8"MÜLLER GAME") / "bsht-data", game));
    EXPECT_FALSE(extract::IsInside(base / "elsewhere" / "bsht-data", game));
}
#endif
