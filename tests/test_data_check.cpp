// The game-data check: the vpk reads bsht-extract's game/ folder and refuses
// one it cannot use, saying why.
#include "app/data_check.h"

#include "app/scene_host.h"
#include "assets/game_data_format.h"
#include "assets/room_file.h"
#include "assets/sound_bank.h"
#include "game/save_store.h"
#include "godot/runtime_scene_loader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

fs::path FreshDir(const std::string& name) {
    const fs::path dir = fs::path(::testing::TempDir()) / ("bsht_data_check_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void WriteFile(const fs::path& dir, const std::string& name, const std::string& text) {
    std::ofstream(dir / name, std::ios::binary) << text;
}

// A manifest in bsht-extract's shape (extract/package.cpp), and one byte per listed file.
std::string Manifest(int format, const std::vector<std::string>& names, const char* eol = "\n") {
    std::string m = std::string("{") + eol + " \"format\": " + std::to_string(format) + "," + eol +
                     " \"tool\": \"bsht-extract 1.0\"," + eol + " \"game\": \"v2.2.0\"," + eol + " \"files\": {";
    for (std::size_t i = 0; i < names.size(); ++i) {
        m += std::string(i ? "," : "") + eol + "  \"" + names[i] + "\": \"00ff\"";
    }
    return m + eol + " }" + eol + "}" + eol;
}

// Every line of the check's message, joined.
std::string Text(const app::GameDataCheck& c) {
    std::string out;
    for (const std::string& l : c.lines) out += l + " | ";
    return out;
}

void WriteGame(const fs::path& dir, int format, const std::vector<std::string>& names) {
    WriteFile(dir, "manifest.json", Manifest(format, names));
    for (const std::string& n : names) WriteFile(dir, n, "x");
}

TEST(DataCheck, PassesWhenTheManifestAndEveryRequiredFileArePresent) {
    const fs::path dir = FreshDir("pass");
    WriteGame(dir, assets::kGameDataFormat, app::RequiredGameFiles());
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_TRUE(c.ok) << Text(c);
    EXPECT_EQ(c.format, assets::kGameDataFormat);
    EXPECT_EQ(c.files, app::RequiredGameFiles().size());
}

TEST(DataCheck, NoFolderSaysNoGameData) {
    const fs::path dir = FreshDir("nofolder") / "game";
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_FALSE(c.ok);
    EXPECT_NE(Text(c).find("No game data"), std::string::npos) << Text(c);
}

TEST(DataCheck, NoManifestSaysNoGameData) {
    const fs::path dir = FreshDir("nomanifest");
    WriteFile(dir, "menu.room", "x");
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_FALSE(c.ok);
    EXPECT_NE(Text(c).find("No game data"), std::string::npos) << Text(c);
}

TEST(DataCheck, DamagedManifestSaysSo) {
    const fs::path dir = FreshDir("damaged");
    WriteFile(dir, "manifest.json", "{ \"tool\": \"bsht-extract 1.0\" ");
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_FALSE(c.ok);
    EXPECT_NE(Text(c).find("damaged"), std::string::npos) << Text(c);
}

TEST(DataCheck, OlderFormatSaysRunTheToolFromThisRelease) {
    const fs::path dir = FreshDir("older");
    WriteGame(dir, assets::kGameDataFormat - 1, app::RequiredGameFiles());
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_FALSE(c.ok);
    EXPECT_NE(Text(c).find("format " + std::to_string(assets::kGameDataFormat - 1)), std::string::npos) << Text(c);
    EXPECT_NE(Text(c).find("needs " + std::to_string(assets::kGameDataFormat)), std::string::npos) << Text(c);
    EXPECT_NE(Text(c).find("this release"), std::string::npos) << Text(c);
}

TEST(DataCheck, NewerFormatSaysUpdateTheVpk) {
    const fs::path dir = FreshDir("newer");
    WriteGame(dir, assets::kGameDataFormat + 1, app::RequiredGameFiles());
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_FALSE(c.ok);
    EXPECT_NE(Text(c).find("newer"), std::string::npos) << Text(c);
}

TEST(DataCheck, MissingListedFileNamesIt) {
    const fs::path dir = FreshDir("listed");
    std::vector<std::string> names = app::RequiredGameFiles();
    WriteGame(dir, assets::kGameDataFormat, names);
    names.push_back("x_extra.json");   // listed, never written
    WriteFile(dir, "manifest.json", Manifest(assets::kGameDataFormat, names));
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_FALSE(c.ok);
    EXPECT_NE(Text(c).find("x_extra.json"), std::string::npos) << Text(c);
}

TEST(DataCheck, MissingRequiredFileNamesItEvenIfTheManifestDoesNot) {
    const fs::path dir = FreshDir("required");
    std::vector<std::string> names = app::RequiredGameFiles();
    names.erase(std::remove(names.begin(), names.end(), "menu.room"), names.end());
    WriteGame(dir, assets::kGameDataFormat, names);
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_FALSE(c.ok);
    EXPECT_NE(Text(c).find("menu.room"), std::string::npos) << Text(c);
}

TEST(DataCheck, ToleratesBomCrlfAndTrailingSlash) {
    const fs::path dir = FreshDir("bom");
    WriteGame(dir, assets::kGameDataFormat, app::RequiredGameFiles());
    WriteFile(dir, "manifest.json", "\xEF\xBB\xBF" + Manifest(assets::kGameDataFormat, app::RequiredGameFiles(), "\r\n"));
    const app::GameDataCheck c = app::CheckGameData(dir.string() + "/");
    EXPECT_TRUE(c.ok) << Text(c);
}

// The longest name in a real game folder is 51 characters; the screen holds 60
// at its scale (960 / 16). The missing file is named whole.
TEST(DataCheck, ALongMissingFileIsNamedWhole) {
    const fs::path dir = FreshDir("longname");
    std::vector<std::string> names = app::RequiredGameFiles();
    WriteGame(dir, assets::kGameDataFormat, names);
    const std::string name = "briefcase_machine_full_1.71s_machine_move_start.ogg";
    names.push_back(name);   // listed, never written
    WriteFile(dir, "manifest.json", Manifest(assets::kGameDataFormat, names));
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_FALSE(c.ok);
    EXPECT_NE(std::find(c.lines.begin(), c.lines.end(), name), c.lines.end()) << Text(c);
}

// An interrupted copy leaves a file that is there but short. bsht-extract
// records each size; the check refuses a file of another size before anything
// loads it (a short room would otherwise quit the Vita with nothing on screen).
// The manifest with a "sizes" object: every file 1 byte, except `big` at 5000.
std::string ManifestWithSizes(const std::vector<std::string>& names, const std::string& big) {
    std::string m = Manifest(assets::kGameDataFormat, names);
    std::string sizes = ",\n \"sizes\": {";
    for (std::size_t i = 0; i < names.size(); ++i) {
        sizes += std::string(i ? "," : "") + "\n  \"" + names[i] + "\": " + (names[i] == big ? "5000" : "1");
    }
    m.insert(m.rfind('}'), sizes + "\n }\n");
    return m;
}

TEST(DataCheck, AFileOfTheWrongSizeIsDamaged) {
    const fs::path dir = FreshDir("short");
    const std::vector<std::string> names = app::RequiredGameFiles();
    WriteGame(dir, assets::kGameDataFormat, names);   // one byte each
    WriteFile(dir, "manifest.json", ManifestWithSizes(names, "full_world.room"));
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_FALSE(c.ok);
    EXPECT_NE(Text(c).find("damaged"), std::string::npos) << Text(c);
    EXPECT_NE(Text(c).find("full_world.room"), std::string::npos) << Text(c);
}

TEST(DataCheck, RightSizesPass) {
    const fs::path dir = FreshDir("sizes");
    const std::vector<std::string> names = app::RequiredGameFiles();
    WriteGame(dir, assets::kGameDataFormat, names);
    WriteFile(dir, "manifest.json", ManifestWithSizes(names, ""));
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    EXPECT_TRUE(c.ok) << Text(c);
}

TEST(DataCheck, MessagesFitTheEightByEightScreen) {
    // 60 characters a line at the data screen's scale.
    const fs::path dir = FreshDir("width");
    std::vector<std::string> names = app::RequiredGameFiles();
    names.erase(std::remove(names.begin(), names.end(), "animations_heaven.json"), names.end());
    WriteGame(dir, assets::kGameDataFormat, names);
    names.push_back("runtime_item_magnifying_glass_faces_a_name_far_longer_than_the_screen.bin");   // listed, never written
    WriteFile(dir, "manifest.json", Manifest(assets::kGameDataFormat, names));
    const app::GameDataCheck c = app::CheckGameData(dir.string());
    ASSERT_FALSE(c.lines.empty());
    for (const std::string& l : c.lines) EXPECT_LE(l.size(), 60u) << l;
}

TEST(DataCheck, RequiredFilesCoverEverySceneFile) {
    const std::vector<std::string> required = app::RequiredGameFiles();
    const auto has = [&](const std::string& n) {
        return std::find(required.begin(), required.end(), n) != required.end();
    };
    for (const char* scene : {"res://scenes/main.tscn", "res://scenes/menu.tscn", "res://scenes/heaven.tscn"}) {
        const app::SceneFiles f = app::FilesForScene(scene, "", "", "");
        for (const std::string& n : {f.room, f.runtime, f.animations, f.ui, f.uiPixels, f.labels}) {
            if (!n.empty()) EXPECT_TRUE(has(n)) << scene << ": " << n;
        }
    }
    for (const char* n : {"sfx.bank", "music_main_gate_modded.ogg", "music_room.ogg", "input_map.json", "ui_fonts.json",
                          "particle_smoke.rgba"}) {
        EXPECT_TRUE(has(n)) << n;
    }
    EXPECT_FALSE(has("menu_logo.rgba"));   // the port's own file, inside the vpk
}

// A room, bank or scene export that changes shape needs a new game folder: a
// player's old one would otherwise pass the check and fail to load. Raise
// kGameDataFormat with it, then move this pin.
TEST(DataCheck, TheFormatMovesWithTheRoomBankAndSceneVersions) {
    const char* raise = "a data version changed: raise assets::kGameDataFormat, then update this pin";
    // 2: ui_fonts.json carries line_metrics and ui_layout*.json each Label's
    // line_spacing (Godot's Label line layout).
    EXPECT_EQ(assets::kGameDataFormat, 2) << raise;
    EXPECT_EQ(assets::kRoomFileVersion, 14u) << raise;
    EXPECT_EQ(assets::kSoundBankVersion, 4u) << raise;
    EXPECT_EQ(godot::kRuntimeSceneFormat, 4) << raise;
}

}  // namespace

namespace {

// Saves live in ux0:data/bsht, the game folder below it. A reset
// (ResetManager -> SaveFileManager.ClearSave -> SaveStore::Clear) removes one
// save by name and never the folder beside it.
TEST(DataCheck, ClearingASaveLeavesTheGameFolder) {
    const fs::path saves = FreshDir("saves");
    const fs::path game = saves / "game";
    fs::create_directories(game);
    WriteGame(game, assets::kGameDataFormat, app::RequiredGameFiles());
    const game::SaveStore store(saves.string());
    ASSERT_TRUE(store.Save("buckshotroulette_main_12", {{"name", std::string("dealer")}}));
    ASSERT_TRUE(store.Clear("buckshotroulette_main_12"));
    EXPECT_FALSE(store.Exists("buckshotroulette_main_12"));
    EXPECT_TRUE(app::CheckGameData(game.string()).ok);
}

}  // namespace
