#include "extract/package.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace {

fs::path Root() { return fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_package"; }

void Touch(const fs::path& p, const std::string& text = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string Slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), {});
}

// A minimal out root with every required file present. Each test has its own:
// ctest runs them as parallel processes.
fs::path MinimalOut(const char* test) {
    const fs::path out = Root() / test / "out";
    fs::remove_all(out);
    const fs::path ex = out / "extracted";
    const fs::path bk = out / "baked";
    for (const char* f : {"full_world.room", "menu.room", "heaven.room", "sfx.bank"}) Touch(bk / f);
    Touch(bk / "music_list.txt", "dealer long.ogg\n");
    for (const char* f : {"particle_smoke.rgba", "runtime_menu.json", "animations_menu.json", "ui_layout_menu.json",
                          "ui_textures_menu.bin", "runtime_heaven.json", "animations_heaven.json",
                          "ui_layout_heaven.json", "ui_textures_heaven.bin", "round_table.json",
                          "animations.json", "runtime_shell.json", "ui_layout.json", "ui_textures.bin",
                          "ui_font_atlas.bin", "dialogue_en.json", "scene_strings.json", "labels3d.json",
                          "input_map.json"}) {
        Touch(ex / f, "{}");
    }
    Touch(ex / "audio_streams.json", "{\"format\": 1, \"streams\": [{\"name\": \"dealer long.ogg\"}]}");
    Touch(ex / "runtime_main.json", "{\"collision_faces\": \"runtime_main_faces.bin\"}");
    Touch(ex / "runtime_main_faces.bin");
    Touch(ex / "ui_fonts.json", "{\"pages\": [{\"file\": \"ui_font_atlas.bin\"}, {\"file\": \"ui_font_page_base_1.bin\"}]}");
    Touch(ex / "ui_font_page_base_1.bin");
    for (int i = 0; i < 9; ++i) Touch(ex / ("runtime_item_" + std::to_string(i) + ".json"), "{}");
    for (const char* c : {"br", "de", "ee", "es_latam", "es", "fr", "it", "ja", "ko", "pl", "pt", "ru", "tr", "ua",
                          "zhs", "zht"}) {
        Touch(ex / (std::string("dialogue_") + c + ".json"), "{}");
    }
    Touch(ex / "audio" / "music_room vol2.ogg");
    Touch(ex / "audio" / "dealer long.ogg");
    Touch(ex / "audio" / "music" / "music main_gate modded.ogg");
    Touch(ex / "audio" / "music" / "music main_room.ogg");
    return out;
}

}  // namespace

TEST(Package, ListsEveryShippedFileWithSpaceFreeNames) {
    std::string error;
    const auto files = extract::ShippedFiles(MinimalOut("ListsEveryShippedFileWithSpaceFreeNames"), error);
    ASSERT_TRUE(error.empty()) << error;
    auto has = [&](const char* to) {
        return std::any_of(files.begin(), files.end(), [&](const auto& f) { return f.to == to; });
    };
    EXPECT_TRUE(has("full_world.room"));
    EXPECT_TRUE(has("music_room.ogg"));
    EXPECT_TRUE(has("music_menu.ogg"));
    EXPECT_TRUE(has("music_main_room.ogg"));
    EXPECT_TRUE(has("dealer_long.ogg"));
    EXPECT_TRUE(has("ui_font_page_base_1.bin"));
    EXPECT_TRUE(has("runtime_main_faces.bin"));
    for (const auto& f : files) EXPECT_EQ(f.to.find(' '), std::string::npos) << f.to;
}

TEST(Package, AMissingRequiredFileIsNamed) {
    const fs::path out = MinimalOut("AMissingRequiredFileIsNamed");
    fs::remove(out / "extracted" / "labels3d.json");
    std::string error;
    extract::ShippedFiles(out, error);
    EXPECT_NE(error.find("labels3d.json"), std::string::npos);
}

// export_audio_streams.gd once wrote no streams for a shipped game: every long
// sound would have played with no length (one frame).
TEST(Package, AStreamListWithNoStreamsIsRefused) {
    const fs::path out = MinimalOut("AStreamListWithNoStreamsIsRefused");
    Touch(out / "extracted" / "audio_streams.json", "{\"format\": 1, \"streams\": []}");
    std::string error;
    extract::ShippedFiles(out, error);
    EXPECT_NE(error.find("audio_streams.json"), std::string::npos) << error;
}

TEST(Package, CollisionFacesAnExportNamesAreRequired) {
    const fs::path out = MinimalOut("CollisionFacesAnExportNamesAreRequired");
    fs::remove(out / "extracted" / "runtime_main_faces.bin");
    std::string error;
    extract::ShippedFiles(out, error);
    EXPECT_NE(error.find("runtime_main_faces.bin"), std::string::npos);
}

TEST(Package, ReplacesAPreviousGameFolderWhole) {
    const fs::path out = MinimalOut("ReplacesAPreviousGameFolderWhole");
    const fs::path game = Root() / "ReplacesAPreviousGameFolderWhole" / "result" / "game";
    Touch(game / "stale.json");
    std::string error;

    ASSERT_TRUE(extract::Package(out, game, "v2.2.0 hotfix 6", error)) << error;

    EXPECT_FALSE(fs::exists(game / "stale.json"));
    EXPECT_TRUE(fs::exists(game / "full_world.room"));
    const std::string manifest = Slurp(game / "manifest.json");
    EXPECT_NE(manifest.find("\"game\": \"v2.2.0 hotfix 6\""), std::string::npos);
    EXPECT_NE(manifest.find("\"full_world.room\""), std::string::npos);
    // Each file's size, so the port can tell a short copy (app/data_check.h).
    const std::string size = std::to_string(fs::file_size(game / "full_world.room"));
    EXPECT_NE(manifest.find("\"sizes\""), std::string::npos) << manifest;
    EXPECT_NE(manifest.find("\"full_world.room\": " + size), std::string::npos) << manifest;
    EXPECT_FALSE(fs::exists(game.parent_path() / "game.new"));
}

// The desktop draws the game's own mouse cursors: they go into game/cursors
// when the exporter wrote them, and the data check does not ask for them.
TEST(Package, ShipsTheCursorsInTheirOwnFolder) {
    const fs::path out = MinimalOut("ShipsTheCursorsInTheirOwnFolder");
    Touch(out / "extracted" / "cursors" / "cursors.json", "{\"cursors\": {}}");
    Touch(out / "extracted" / "cursors" / "cursor xp_point.raw");
    const fs::path game = Root() / "ShipsTheCursorsInTheirOwnFolder" / "result" / "game";
    std::string error;

    ASSERT_TRUE(extract::Package(out, game, "v2.2.0 hotfix 6", error)) << error;

    EXPECT_TRUE(fs::exists(game / "cursors" / "cursors.json"));
    EXPECT_TRUE(fs::exists(game / "cursors" / "cursor xp_point.raw"));
    EXPECT_NE(Slurp(game / "manifest.json").find("\"cursors/cursors.json\""), std::string::npos);
}

TEST(Package, NoCursorsIsNotAnError) {
    const fs::path out = MinimalOut("NoCursorsIsNotAnError");
    std::string error;
    const auto files = extract::ShippedFiles(out, error);
    EXPECT_TRUE(error.empty()) << error;
}

// The data is staged in the work folder (out's parent here), and a game.new
// the player keeps beside game/ is not the tool's to delete.
TEST(Package, StagesInTheWorkFolderAndLeavesOtherFoldersAlone) {
    const fs::path out = MinimalOut("StagesInTheWorkFolder");
    const fs::path game = Root() / "StagesInTheWorkFolder" / "result" / "game";
    Touch(game.parent_path() / "game.new" / "mine.txt");
    Touch(game / "stale.json");
    std::string error;

    ASSERT_TRUE(extract::Package(out, game, "v2.2.0 hotfix 6", error)) << error;

    EXPECT_TRUE(fs::exists(game.parent_path() / "game.new" / "mine.txt"));
    EXPECT_TRUE(fs::exists(game / "full_world.room"));
    EXPECT_FALSE(fs::exists(game / "stale.json"));
    EXPECT_FALSE(fs::exists(out.parent_path() / "game.new"));
    EXPECT_FALSE(fs::exists(out.parent_path() / "game.old"));
}

// The version can be a project path in dev runs; the manifest stays JSON.
TEST(Package, TheManifestEscapesWhatItQuotes) {
    const fs::path out = MinimalOut("TheManifestEscapesWhatItQuotes");
    const fs::path game = Root() / "TheManifestEscapesWhatItQuotes" / "result" / "game";
    std::string error;

    ASSERT_TRUE(extract::Package(out, game, "project E:\\a \"b\"", error)) << error;

    EXPECT_NE(Slurp(game / "manifest.json").find("\"game\": \"project E:\\\\a \\\"b\\\"\""), std::string::npos);
}

namespace {

// stage_game packages the repo's assets/ for a test game folder; its staging
// goes to a work folder of its own, not beside assets/.
TEST(Package, AWorkFolderTakesTheStaging) {
    const fs::path out = MinimalOut("AWorkFolderTakesTheStaging");
    const fs::path work = Root() / "AWorkFolderTakesTheStaging" / "work";
    const fs::path game = Root() / "AWorkFolderTakesTheStaging" / "result" / "game";
    fs::create_directories(work);
    std::string error;

    ASSERT_TRUE(extract::Package(out, game, "from assets", error, work)) << error;

    EXPECT_TRUE(fs::exists(game / "manifest.json"));
    EXPECT_TRUE(fs::is_empty(work));
    for (const char* name : {"game.new", "game.old"}) EXPECT_FALSE(fs::exists(out.parent_path() / name)) << name;
}

}  // namespace
