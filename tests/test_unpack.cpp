#include "extract/unpack.h"
#include "extract/long_path.h"
#include "extract/pack_reader.h"
#include "extract/remove_tree.h"
#include "extract_test_pack.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

fs::path Root() { return fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_unpack"; }

std::string Slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

}  // namespace

TEST(Unpack, RefusesPathsThatLeaveTheFolder) {
    EXPECT_EQ(extract::SafeRelativePath("res://scenes/main.tscn.remap"), "scenes/main.tscn.remap");
    EXPECT_EQ(extract::SafeRelativePath("res://.godot/imported/a.ctex"), ".godot/imported/a.ctex");
    for (const char* bad : {"res://../x", "res://a/../../x", "/etc/x", "res:///abs", "res://C:/x",
                            "res://a\\b", "res://", "res://a//b", "res://./a", "scripts/x.gd"}) {
        EXPECT_FALSE(extract::SafeRelativePath(bad).has_value()) << bad;
    }
}

TEST(Unpack, WritesEveryFileUnderTheGameFolder) {
    const auto pck = testpack::Write(Root() / "in.pck",
                                     testpack::Build({{"res://project.binary", "PB"}, {"res://audio/a.ogg.import", "I"}}));
    extract::PackInfo info;
    ASSERT_EQ(extract::ReadPack(pck.string(), info), extract::PackError::None);
    fs::remove_all(Root() / "game");
    extract::UnpackStats stats;
    std::string error;

    ASSERT_TRUE(extract::Unpack(pck.string(), info, Root() / "game", stats, error)) << error;

    EXPECT_EQ(stats.files, 2u);
    EXPECT_EQ(Slurp(Root() / "game" / "audio" / "a.ogg.import"), "I");
}

TEST(Unpack, AnEscapingEntryStopsTheUnpackAndWritesNothingOutside) {
    const auto pck = testpack::Write(Root() / "evil.pck", testpack::Build({{"res://../outside.txt", "x"}}));
    extract::PackInfo info;
    ASSERT_EQ(extract::ReadPack(pck.string(), info), extract::PackError::None);
    extract::UnpackStats stats;
    std::string error;

    EXPECT_FALSE(extract::Unpack(pck.string(), info, Root() / "game2", stats, error));

    EXPECT_FALSE(fs::exists(Root() / "outside.txt"));
    EXPECT_NE(error.find("res://../outside.txt"), std::string::npos);
}

TEST(Unpack, HandlesNonAsciiAndSpacesInPaths) {
    const fs::path dir = Root() / fs::path(u8"Müller dir");
    const auto pck = testpack::Write(dir / fs::path(u8"Buckshot Roulette.exe"),
                                     testpack::Build({{"res://music/music main_gate modded.ogg.import", "M"}}));
    extract::PackInfo info;
    const std::u8string u8 = pck.u8string();
    const std::string utf8(u8.begin(), u8.end());
    ASSERT_EQ(extract::ReadPack(utf8, info), extract::PackError::None);
    extract::UnpackStats stats;
    std::string error;

    ASSERT_TRUE(extract::Unpack(utf8, info, dir / "game", stats, error)) << error;

    EXPECT_EQ(Slurp(dir / "game" / "music" / "music main_gate modded.ogg.import"), "M");
}

TEST(Unpack, TheOverrideStubsSteamAndKeepsUserDataPrivate) {
    const fs::path game = Root() / "ovr";
    fs::create_directories(game / ".godot");
    std::ofstream(game / ".godot" / "extension_list.cfg") << "res://addons/godotsteam/godotsteam.gdextension\n";

    extract::WriteOverride(game, true);
    extract::DisableExtensions(game);

    const std::string cfg = Slurp(game / "override.cfg");
    EXPECT_NE(cfg.find("config/use_custom_user_dir=true"), std::string::npos);
    EXPECT_NE(cfg.find("Steam=\"*res://tools/godot/steam_stub.gd\""), std::string::npos);
    EXPECT_NE(cfg.find("GlobalSteam=\"*res://tools/godot/steam_off.gd\""), std::string::npos);
    EXPECT_FALSE(fs::exists(game / ".godot" / "extension_list.cfg"));
}

// v2.2's pack paths run past Windows' 260 characters (multiplayer/...). The
// tool writes and deletes them with the long-path prefix; Godot adds it itself.
TEST(Unpack, WritesAndDeletesPathsPastTheWindowsLimit) {
    const std::string folder(90, 'd');
    const std::string file = std::string(150, 'f') + ".import";
    const fs::path dir = Root() / "long";
    const auto pck = testpack::Write(dir / "game.pck", testpack::Build({{"res://" + folder + "/" + file, "L"}}));
    extract::PackInfo info;
    ASSERT_EQ(extract::ReadPack(pck.string(), info), extract::PackError::None);
    extract::UnpackStats stats;
    std::string error;

    ASSERT_TRUE(extract::Unpack(pck.string(), info, dir / "game", stats, error)) << error;
    const fs::path written = dir / "game" / folder / file;
    EXPECT_GT(fs::absolute(written).wstring().size(), 260u);
    EXPECT_EQ(Slurp(extract::LongPath(written)), "L");

    ASSERT_TRUE(extract::RemoveTree(dir / "game", error)) << error;
    EXPECT_FALSE(fs::exists(extract::LongPath(dir / "game")));
}

// v2.2's PipelineManager takes the game's OpenGL path when Godot has no
// rendering device, as in a headless run: it hides the room for 0.1 s and
// halves the lights. The unpacked copy gets our stand-in instead.
TEST(Unpack, TheOpenGlPathIsReplacedInTheUnpackedCopy) {
    const fs::path game = Root() / "gl" / "game";
    const fs::path ours = Root() / "gl" / "exporters";
    fs::create_directories(game / "scripts");
    fs::create_directories(ours);
    std::ofstream(game / "scripts" / "PipelineManager.gd") << "the game's";
    std::ofstream(ours / "pipeline_manager.gd") << "ours";
    std::vector<std::string> replaced;
    std::string error;

    ASSERT_TRUE(extract::ReplaceScripts(ours, game, replaced, error)) << error;

    EXPECT_EQ(Slurp(game / "scripts" / "PipelineManager.gd"), "ours");
    EXPECT_EQ(replaced, std::vector<std::string>{"scripts/PipelineManager.gd"});
}

// v1.2 has no PipelineManager: nothing is added.
TEST(Unpack, AGameWithoutThatScriptIsLeftAlone) {
    const fs::path game = Root() / "nogl" / "game";
    const fs::path ours = Root() / "nogl" / "exporters";
    fs::create_directories(game / "scripts");
    fs::create_directories(ours);
    std::ofstream(ours / "pipeline_manager.gd") << "ours";
    std::vector<std::string> replaced;
    std::string error;

    ASSERT_TRUE(extract::ReplaceScripts(ours, game, replaced, error)) << error;

    EXPECT_TRUE(replaced.empty());
    EXPECT_FALSE(fs::exists(game / "scripts" / "PipelineManager.gd"));
}

// A dev project's override.cfg is the tool's only with the marker on its first
// line; a leftover from a killed run can then be told from one written by
// hand.
TEST(Unpack, OurOverrideCarriesAMarkTheToolRecognises) {
    const fs::path game = Root() / "marked";
    fs::create_directories(game);
    extract::WriteOverride(game, false);
    EXPECT_EQ(Slurp(game / "override.cfg").rfind(extract::kOverrideMark, 0), 0u);
    EXPECT_TRUE(extract::OverrideIsOurs(game));

    std::ofstream(game / "override.cfg", std::ios::trunc) << "[application]\nconfig/name=\"mine\"\n";
    EXPECT_FALSE(extract::OverrideIsOurs(game));
    fs::remove(game / "override.cfg");
    EXPECT_FALSE(extract::OverrideIsOurs(game));
}

namespace {

// --project on the reference game (made by --unpack-to, kept private by
// tools/godot/ref_game.sh): the run must not leave it without its override,
// and Godot must find the Steam stub while it runs (no GodotSteam there).
TEST(ProjectOverride, TheReferenceGamesOverrideIsRestoredAfterTheRun) {
    const fs::path game = Root() / "reference";
    fs::remove_all(game);
    fs::create_directories(game / ".godot");   // extensions disabled: no extension_list.cfg
    const std::string theirs = std::string(extract::kOverrideMark) +
                               "\n\n[application]\n\nconfig/use_custom_user_dir=true\n"
                               "config/custom_user_dir_name=\"bsht_parity_user\"\n";
    std::ofstream(game / "override.cfg", std::ios::binary) << theirs;
    {
        extract::ProjectOverride o(game);
        ASSERT_TRUE(o.Owned());
        const std::string during = Slurp(game / "override.cfg");
        EXPECT_NE(during.find("res://tools/godot/steam_stub.gd"), std::string::npos) << "Steam stubbed";
        EXPECT_NE(during.find("steam_off.gd"), std::string::npos);
        EXPECT_NE(during.find("use_custom_user_dir=true"), std::string::npos);
    }
    EXPECT_EQ(Slurp(game / "override.cfg"), theirs) << "the reference game keeps its own";
}

TEST(ProjectOverride, AProjectWithGodotSteamGetsNoStubAndLosesTheFileAfter) {
    const fs::path game = Root() / "with_steam";
    fs::remove_all(game);
    fs::create_directories(game / ".godot");
    std::ofstream(game / ".godot" / "extension_list.cfg") << "res://addons/godotsteam/godotsteam.gdextension\n";
    {
        extract::ProjectOverride o(game);
        ASSERT_TRUE(o.Owned());
        EXPECT_EQ(Slurp(game / "override.cfg").find("steam_stub.gd"), std::string::npos);
    }
    EXPECT_FALSE(fs::exists(game / "override.cfg"));
}

TEST(ProjectOverride, ADevelopersOwnOverrideIsLeftAlone) {
    const fs::path game = Root() / "foreign";
    fs::remove_all(game);
    fs::create_directories(game);
    std::ofstream(game / "override.cfg", std::ios::binary) << "[application]\nconfig/name=\"mine\"\n";
    {
        extract::ProjectOverride o(game);
        EXPECT_FALSE(o.Owned());
    }
    EXPECT_EQ(Slurp(game / "override.cfg"), "[application]\nconfig/name=\"mine\"\n");
}

}  // namespace
